/// sim_debug — headless blueprint simulator with signal probing.
///
/// Usage:
///   sim_debug <blueprint.blueprint> [options]
///
/// Options:
///   -n <steps>          Number of simulation steps (default: 600 = 10s at 60Hz)
///   -dt <seconds>       Timestep (default: 1/60)
///   -p <node.port>      Probe a signal (repeatable). Use "node_id.port_name"
///   -P                  Probe ALL signals (dump full signal table)
///   -every <N>          Print probes every N steps (default: 60)
///   -map                Print the full port→signal mapping and exit
///   -json               Dump the generated simulator JSON and exit
///   -q                  Quiet: suppress spdlog output
///
/// Examples:
///   sim_debug GSC.blueprint -p bus_2.v -p pi_1.output -p variableconductance_1.cmd -n 600
///   sim_debug GSC.blueprint -map
///   sim_debug GSC.blueprint -json
///   sim_debug GSC.blueprint -P -every 120 -n 600

#include "core/solvers/jit/simulator.h"
#include "json_parser/json_parser.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/interface/node_port_projection.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <cstring>

// ============================================================================
// Helpers duplicated from editor (persist.cpp / document.cpp) to avoid
// pulling in the entire editor + ImGui dependency chain.
// ============================================================================

static const char* sim_port_type_str(PortType t) {
    switch (t) {
        case PortType::V:           return "V";
        case PortType::I:           return "I";
        case PortType::Bool:        return "Bool";
        case PortType::RPM:         return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure:    return "Pressure";
        case PortType::Position:    return "Position";
        case PortType::Any:
        default:                    return "Any";
    }
}

/// Build simulation JSON from a bp2::Blueprint (mirrors Document::build_simulation_json).
static std::string build_simulation_json(const bp2::Blueprint& bp,
                                          ui::StringInterner& interner,
                                          const bp2::PathArena& arena) {
    using json = nlohmann::json;

    json out = json::object();
    out["templates"] = json::object();

    // --- devices ---
    json devices = json::array();
    std::set<std::string> emitted_ids;
    // Track skipped embedded proxy nodes for connection rewriting
    std::set<std::string> skipped_embedded_proxies;

    for (const bp2::Blueprint::Node& n : bp.nodes()) {
        // Embedded blueprint proxy nodes with materialized children: skip the
        // proxy (its internal nodes are already flattened into the blueprint).
        if (n.view.expandable) {
            const auto* nested = bp.find_nested(n.semantic.id);
            if (nested && nested->is_embedded()) {
                bool has_materialized_children = false;
                const std::string parent_id(interner.resolve(n.semantic.id));
                for (const auto& child : bp.nodes()) {
                    if (child.layout.layout_group == parent_id) {
                        has_materialized_children = true;
                        break;
                    }
                }
                if (has_materialized_children) {
                    skipped_embedded_proxies.insert(parent_id);
                    continue;
                }
            }
        }

        // Non-embedded expandable (composite) nodes — emit them as regular
        // devices so that parse_json_impl() can expand them via TypeRegistry.

        std::string nid(interner.resolve(n.semantic.id));
        if (!emitted_ids.insert(nid).second) continue;

        json device = json::object();
        device["name"]          = nid;
        device["template_name"] = "";
        device["classname"]     = std::string(interner.resolve(n.semantic.type));
        if (!n.view.render_hint.empty())
            device["render_hint"] = n.view.render_hint;
        device["priority"]  = "med";
        device["bucket"]    = nullptr;
        device["critical"]  = false;

        json ports = json::object();
        for (const auto& p : bp2::derive_input_ports(n.semantic.iface)) {
            ports[std::string(interner.resolve(p.name))] = {
                {"direction", "In"},
                {"type", sim_port_type_str(p.type)}
            };
        }
        for (const auto& p : bp2::derive_output_ports(n.semantic.iface)) {
            ports[std::string(interner.resolve(p.name))] = {
                {"direction", "Out"},
                {"type", sim_port_type_str(p.type)}
            };
        }
        device["ports"] = std::move(ports);

        json params = json::object();
        for (const auto& [k, v] : n.semantic.params)
            params[std::string(interner.resolve(k))] = std::to_string(v);
        for (const auto& [k, v] : n.semantic.string_params)
            params[k] = v;
        if (!params.empty())
            device["params"] = std::move(params);

        devices.push_back(std::move(device));
    }
    out["devices"] = std::move(devices);

    // --- connections ---
    auto path_to_node_port = [&](const bp2::Path& path)
            -> std::pair<std::string, std::string> {
        if (path.kind() != bp2::PathKind::Port) return {};
        ui::InternedId port_name = path.segment();
        bp2::Path parent = arena.parent(path);
        if (parent.kind() != bp2::PathKind::Node) return {};
        ui::InternedId node_id = parent.segment();
        return {std::string(interner.resolve(node_id)),
                std::string(interner.resolve(port_name))};
    };

    // Build a lookup map for bridge nodes of skipped embedded proxies.
    // Bridge nodes are BlueprintInput/BlueprintOutput nodes whose ID follows
    // the canonical colon convention: "proxy_id:port_name".
    // Key: "proxy_id.port_name" -> bridge node device id
    std::map<std::string, std::string> proxy_port_to_bridge;
     if (!skipped_embedded_proxies.empty()) {
         for (const bp2::Blueprint::Node& n : bp.nodes()) {
             std::string nid(interner.resolve(n.semantic.id));
            // Match colon-convention IDs: "proxy_id:port_name"
            for (const auto& proxy_id : skipped_embedded_proxies) {
                if (nid.size() > proxy_id.size() + 1
                    && nid.compare(0, proxy_id.size(), proxy_id) == 0
                    && nid[proxy_id.size()] == ':') {
                    std::string port_name = nid.substr(proxy_id.size() + 1);
                    std::string key = proxy_id + "." + port_name;
                    proxy_port_to_bridge[key] = nid;
                }
            }
        }
    }

    // Rewrite connections referencing skipped embedded proxy nodes.
    // Look up the actual bridge node ID by (proxy_id, port_name).

    json connections = json::array();
    std::set<std::string> emitted_conn;

    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        auto [sn, sp] = path_to_node_port(w.source);
        auto [tn, tp] = path_to_node_port(w.target);
        if (sn.empty() || sp.empty() || tn.empty() || tp.empty()) continue;

        // Rewrite endpoints referencing skipped embedded proxy nodes
        if (skipped_embedded_proxies.count(sn)) {
            auto it = proxy_port_to_bridge.find(sn + "." + sp);
            if (it != proxy_port_to_bridge.end()) {
                sn = it->second;
                sp = "ext";
            }
        }
        if (skipped_embedded_proxies.count(tn)) {
            auto it = proxy_port_to_bridge.find(tn + "." + tp);
            if (it != proxy_port_to_bridge.end()) {
                tn = it->second;
                tp = "ext";
            }
        }

        std::string key = sn + "." + sp + "→" + tn + "." + tp;
        if (!emitted_conn.insert(key).second) continue;

        json c = json::object();
        c["from"] = sn + "." + sp;
        c["to"]   = tn + "." + tp;
        connections.push_back(std::move(c));
    }
    out["connections"] = std::move(connections);

    return out.dump(2);
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    // --- Parse CLI ---
    if (argc < 2) {
        std::cerr << "Usage: sim_debug <file.blueprint> [options]\n"
                  << "  -n <steps>       Steps to run        (default 600)\n"
                  << "  -dt <sec>        Timestep             (default 1/60)\n"
                  << "  -p <node.port>   Probe signal         (repeatable)\n"
                  << "  -P               Probe ALL signals\n"
                  << "  -every <N>       Print every N steps  (default 60)\n"
                  << "  -map             Print port→signal map and exit\n"
                  << "  -json            Dump simulator JSON and exit\n"
                  << "  -q               Quiet (suppress spdlog)\n";
        return 1;
    }

    std::string bp_path = argv[1];
    int steps      = 600;
    double dt = 1.0 / 60.0;
    int print_every = 60;
    bool probe_all = false;
    bool dump_map  = false;
    bool dump_json = false;
    bool quiet     = false;
    std::vector<std::string> probes;

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "-n") == 0 && i+1 < argc) {
            steps = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-dt") == 0 && i+1 < argc) {
            dt = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "-p") == 0 && i+1 < argc) {
            probes.push_back(argv[++i]);
        } else if (std::strcmp(argv[i], "-P") == 0) {
            probe_all = true;
        } else if (std::strcmp(argv[i], "-every") == 0 && i+1 < argc) {
            print_every = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-map") == 0) {
            dump_map = true;
        } else if (std::strcmp(argv[i], "-json") == 0) {
            dump_json = true;
        } else if (std::strcmp(argv[i], "-q") == 0) {
            quiet = true;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            return 1;
        }
    }

    if (quiet) spdlog::set_level(spdlog::level::off);

    // --- Load input file ---
    std::ifstream file(bp_path);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << bp_path << "\n";
        return 1;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    std::string raw_json = buf.str();

    std::string sim_json;
    bool is_json_input = (bp_path.size() >= 5 &&
                          bp_path.substr(bp_path.size() - 5) == ".json");

    if (is_json_input) {
        // Raw simulator JSON — use directly
        sim_json = raw_json;
    } else {
        // Blueprint file — decode and convert
        ui::StringInterner interner;
        bp2::PathArena arena(interner);
        TypeRegistry registry = load_type_registry("library/");

        bp2::DecodeError err;
        auto bp = bp2::BlueprintCodec::decode(raw_json, interner, arena, registry, &err);
        if (!bp) {
            std::cerr << "Failed to decode blueprint: " << err.message << "\n";
            return 1;
        }

        sim_json = build_simulation_json(*bp, interner, arena);
    }

    if (dump_json) {
        std::cout << sim_json << "\n";
        return 0;
    }

    // --- Start simulator ---
    JIT_Simulator sim;
    try {
        sim.start_from_json(sim_json);
    } catch (const std::exception& e) {
        std::cerr << "Simulator start failed: " << e.what() << "\n";
        return 1;
    }

    if (!sim.is_running()) {
        std::cerr << "Simulator did not start.\n";
        return 1;
    }

    // --- Build probe list ---
    // Parse the sim JSON to get port→signal map for the -map flag
    auto ctx = parse_json(sim_json);

    // Rebuild port→signal via build_systems_dev for the map
    std::vector<std::pair<std::string,std::string>> conn_pairs;
    for (const auto& c : ctx.connections)
        conn_pairs.push_back({c.from, c.to});
    auto build = build_systems_dev(ctx.devices, conn_pairs);

    if (dump_map) {
        // Group by signal index
        std::map<uint32_t, std::vector<std::string>> sig_to_ports;
        for (const auto& [port, sig] : build.port_to_signal)
            sig_to_ports[sig].push_back(port);

        std::cout << "=== Port → Signal Map ===\n";
        std::cout << "Signals: " << build.signal_count
                  << "  Fixed: " << build.fixed_signals.size() << "\n\n";

        for (auto& [sig, ports] : sig_to_ports) {
            std::sort(ports.begin(), ports.end());
            bool fixed = std::binary_search(
                build.fixed_signals.begin(), build.fixed_signals.end(), sig);
            std::cout << "  signal[" << std::setw(3) << sig << "]"
                      << (fixed ? " FIXED" : "      ") << " : ";
            for (size_t j = 0; j < ports.size(); ++j) {
                if (j) std::cout << ", ";
                std::cout << ports[j];
            }
            std::cout << "\n";
        }
        return 0;
    }

    // If probe_all, collect all unique port names grouped by node
    if (probe_all) {
        std::set<std::string> all;
        for (const auto& [port, sig] : build.port_to_signal)
            all.insert(port);
        probes.assign(all.begin(), all.end());
    }

    // Validate probes
    for (const auto& p : probes) {
        if (build.port_to_signal.find(p) == build.port_to_signal.end()) {
            std::cerr << "WARNING: probe '" << p << "' not found in signal map.\n";
            // Don't fail — might be a typo, still run simulation
        }
    }

    if (probes.empty()) {
        std::cerr << "No probes specified. Use -p <node.port> or -P to probe all.\n";
        return 1;
    }

    // --- Print header ---
    std::cout << std::fixed << std::setprecision(6);
    std::cout << std::setw(8) << "step" << std::setw(10) << "time";
    for (const auto& p : probes)
        std::cout << std::setw(20) << p;
    std::cout << "\n";

    // Separator
    std::cout << std::string(8 + 10 + 20 * probes.size(), '-') << "\n";

    // --- Run simulation ---
    auto print_row = [&](int step_num) {
        std::cout << std::setw(8) << step_num
                  << std::setw(10) << std::setprecision(4) << sim.get_time();
        for (const auto& p : probes) {
            float v = sim.get_wire_voltage(p);
            std::cout << std::setw(20) << std::setprecision(6) << v;
        }
        std::cout << "\n";
    };

    // Print initial state
    print_row(0);

    for (int i = 1; i <= steps; ++i) {
        sim.step(dt);
        if (i % print_every == 0 || i == steps)
            print_row(i);
    }

    std::cout << "\n=== Done: " << steps << " steps, "
              << std::setprecision(4) << sim.get_time() << "s simulated ===\n";

    return 0;
}
