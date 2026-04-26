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
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "blueprint_v2/path/path.h"
#include "core/solvers/common/signal_key.h"
#include "io/json/component_registry_json_loader.h"
#include "core/strings/interned_id.h"
#include <nlohmann/json.hpp>
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

namespace {

void log_warning(bool quiet, const std::string& message) {
    if (!quiet) {
        std::cerr << "[sim_debug] warning: " << message << '\n';
    }
}

} // namespace

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

    // --- Load input file ---
    std::ifstream file(bp_path);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << bp_path << "\n";
        return 1;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    std::string raw_json = buf.str();

    // Blueprint file — canonical path via Flattener + elaborate_for_jit.
    core::StringInterner interner;
    bp2::PathArena arena(interner);
    ComponentRegistry registry = load_component_registry("library/");

    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(raw_json, interner, arena, registry, &err);
    if (!bp) {
        std::cerr << "Failed to decode blueprint: " << err.message << "\n";
        return 1;
    }

    bp2::BlueprintLibrary library;
    for (const auto& [classname, def] : registry.all_types()) {
        try {
            auto loaded = bp2::blueprint_from_type_definition(def, interner, registry);
            library.add(interner.intern(classname), std::move(loaded));
        } catch (const std::exception& e) {
            log_warning(quiet, "Failed to build blueprint '" + classname + "': " + e.what());
        }
    }

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(*bp, arena);

    if (dump_json) {
        JitBuildInput jit_input = bp2::elaboration::elaborate_for_jit(netlist, arena, interner, registry);
        
        // Build canonical JSON output
        nlohmann::json out = nlohmann::json::object();
        out["signal_count"] = jit_input.signal_count;
        
        // Devices array
        nlohmann::json devices_array = nlohmann::json::array();
        for (const auto& dev : jit_input.devices) {
            devices_array.push_back(dev.name);
        }
        out["devices"] = devices_array;
        
        // Signals grouped by signal index
        std::map<uint32_t, std::vector<std::string>> sig_to_ports;
        for (const auto& [port_id, sig] : jit_input.port_to_signal) {
            std::string_view port = jit_input.signal_key_interner.resolve(port_id);
            sig_to_ports[sig].push_back(std::string(port));
        }
        nlohmann::json signals_obj = nlohmann::json::object();
        for (const auto& [sig_idx, ports] : sig_to_ports) {
            nlohmann::json ports_array = nlohmann::json::array();
            for (const auto& p : ports) {
                ports_array.push_back(p);
            }
            signals_obj[std::to_string(sig_idx)] = ports_array;
        }
        out["signals"] = signals_obj;
        
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    JitBuildInput build_input = bp2::elaboration::elaborate_for_jit(netlist, arena, interner, registry);

    if (dump_map) {
        // Group by signal index
        std::map<uint32_t, std::vector<std::string>> sig_to_ports;
        for (const auto& [port_id, sig] : build_input.port_to_signal) {
            std::string_view port = build_input.signal_key_interner.resolve(port_id);
            sig_to_ports[sig].push_back(std::string(port));
        }

        std::cout << "=== Port → Signal Map ===\n";
        std::cout << "Signals: " << build_input.signal_count << "\n\n";

        for (auto& [sig, ports] : sig_to_ports) {
            std::sort(ports.begin(), ports.end());
            std::cout << "  signal[" << std::setw(3) << sig << "]"
                      << "       : ";
            for (size_t j = 0; j < ports.size(); ++j) {
                if (j) std::cout << ", ";
                std::cout << ports[j];
            }
            std::cout << "\n";
        }
        return 0;
    }

    // --- Start simulator ---
    JIT_Simulator sim;
    try {
        sim.start(build_input);
    } catch (const std::exception& e) {
        std::cerr << "Simulator start failed: " << e.what() << "\n";
        return 1;
    }

    if (!sim.is_running()) {
        std::cerr << "Simulator did not start.\n";
        return 1;
    }

    // If probe_all, collect all unique port names
    if (probe_all) {
        std::set<std::string> all;
        for (const auto& [port_id, sig] : build_input.port_to_signal) {
            std::string_view port = build_input.signal_key_interner.resolve(port_id);
            all.insert(std::string(port));
        }
        probes.assign(all.begin(), all.end());
    }

    // Validate probes
    for (const auto& p : probes) {
        const core::InternedId key = build_input.signal_key_interner.lookup(p);
        if (build_input.port_to_signal.find(key) == build_input.port_to_signal.end()) {
            std::cerr << "WARNING: probe '" << p << "' not found in signal map.\n";
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
            float v = sim.get_signal_value(sim.signal_key_interner().lookup(p));
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
