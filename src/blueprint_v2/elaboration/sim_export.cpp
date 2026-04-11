#include "sim_export.h"

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/node_port_projection.h"
#include "core/solvers/common/signal_key.h"

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace bp2::elaboration {

namespace {

const char* sim_port_type_str(PortType t) {
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

nlohmann::json emit_device_json(const bp2::Blueprint::Node& node,
                               const std::string& device_id,
                               const ui::StringInterner& interner,
                               const TypeRegistry* type_registry) {
    using json = nlohmann::json;

    const std::string classname(interner.resolve(node.semantic.type));

    json device = json::object();
    device["name"] = device_id;
    device["template_name"] = "";
    device["classname"] = classname;
    device["priority"] = "med";
    device["bucket"] = nullptr;
    device["critical"] = false;

    json ports = json::object();
    for (const auto& p : bp2::derive_input_ports(node.semantic.iface)) {
        ports[std::string(interner.resolve(p.name))] = {
            {"direction", "In"},
            {"type", sim_port_type_str(p.type)}
        };
    }
    for (const auto& p : bp2::derive_output_ports(node.semantic.iface)) {
        ports[std::string(interner.resolve(p.name))] = {
            {"direction", "Out"},
            {"type", sim_port_type_str(p.type)}
        };
    }
    device["ports"] = std::move(ports);

    const TypeDefinition* type_def = nullptr;
    if (type_registry) {
        type_def = type_registry->get(classname);
    }
    auto is_visual_only = [&](const std::string& key) -> bool {
        if (!type_def) return false;
        auto it = type_def->param_schema.find(key);
        return it != type_def->param_schema.end() && it->second.visual_only;
    };
    auto is_int_param = [&](const std::string& key) -> bool {
        if (!type_def) return false;
        auto it = type_def->param_schema.find(key);
        return it != type_def->param_schema.end() && it->second.type == ParamSchemaType::Int;
    };

    json params = json::object();
    for (const auto& [k, v] : node.semantic.params) {
        std::string key = std::string(interner.resolve(k));
        if (is_visual_only(key)) continue;
        if (is_int_param(key)) {
            params[key] = std::to_string(static_cast<long long>(v));
        } else {
            params[key] = std::to_string(v);
        }
    }
    for (const auto& [k, v] : node.semantic.string_params) {
        if (is_visual_only(k)) continue;
        params[k] = v;
    }
    if (!params.empty()) {
        device["params"] = std::move(params);
    }

    return device;
}

std::string node_id_from_path(Path node_path, PathArena& arena, const ui::StringInterner& interner) {
    std::vector<std::string> segments;
    Path cur = node_path;
    while (cur.kind() != PathKind::Root) {
        if (cur.kind() == PathKind::Nested || cur.kind() == PathKind::Node) {
            segments.emplace_back(interner.resolve(cur.segment()));
        }
        cur = arena.parent(cur);
    }

    std::string out;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        if (!out.empty()) out.push_back(':');
        out += *it;
    }
    return out;
}

} // namespace

SimulationExport to_simulation_export(
    const FlatNetlist& netlist,
    PathArena& arena,
    const ui::StringInterner& interner,
    const TypeRegistry* type_registry) {

    SimulationExport out;
    out.devices = nlohmann::json::array();
    out.connections = nlohmann::json::array();

    std::set<std::string> emitted_ids;
    for (const auto& comp : netlist.components) {
        bp2::Blueprint::Node node;
        node.semantic.id = comp.path.segment();
        node.semantic.type = comp.type;
        node.semantic.params = comp.params;
        node.semantic.string_params = comp.string_params;
        node.semantic.iface = bp2::Interface(comp.ports);

        const std::string dev_id = node_id_from_path(comp.path, arena, interner);
        if (!emitted_ids.insert(dev_id).second) {
            continue;
        }
        out.devices.push_back(emit_device_json(node, dev_id, interner, type_registry));
    }

    // No bridge rewrite needed — the flattener now resolves wire endpoints
    // directly to bridge ext ports, producing a self-consistent IR where
    // every signal endpoint references an emitted leaf device.

    std::set<std::string> emitted_keys;
    for (const auto& sig : netlist.signals) {
        std::vector<std::string> endpoints;
        std::set<std::string> unique;
        for (Path port_path : sig.connected_ports) {
            if (port_path.kind() != PathKind::Port) continue;
            Path parent = arena.parent(port_path);
            if (parent.kind() != PathKind::Node) continue;
            const std::string node_id = node_id_from_path(parent, arena, interner);
            const std::string port_name(interner.resolve(port_path.segment()));

            const std::string key = signal_key::make_node_port_key(node_id, port_name);
            if (unique.insert(key).second) {
                endpoints.push_back(key);
            }
        }
        if (endpoints.size() < 2) continue;

        const std::string& anchor = endpoints.front();
        for (size_t i = 1; i < endpoints.size(); ++i) {
            const std::string edge = anchor + "->" + endpoints[i];
            if (!emitted_keys.insert(edge).second) continue;
            nlohmann::json conn = nlohmann::json::object();
            conn["from"] = anchor;
            conn["to"] = endpoints[i];
            out.connections.push_back(std::move(conn));
        }
    }

    return out;
}

// ==================================================================
// elaborate_for_jit — direct FlatNetlist → JitBuildInput (no JSON)
// ==================================================================

JitBuildInput elaborate_for_jit(
    const FlatNetlist& netlist,
    PathArena& arena,
    const ui::StringInterner& interner,
    const TypeRegistry* type_registry) {

    JitBuildInput result;

    // --- Convert components to DeviceInstances ---
    std::set<std::string> emitted_ids;
    for (const auto& comp : netlist.components) {
        const std::string dev_id = node_id_from_path(comp.path, arena, interner);
        if (!emitted_ids.insert(dev_id).second) {
            continue;
        }

        const std::string classname(interner.resolve(comp.type));

        DeviceInstance dev;
        dev.name = dev_id;
        dev.classname = classname;
        dev.priority = "med";
        dev.critical = false;

        // Ports: derive from the flattened PortDescriptors
        for (const auto& pd : comp.ports) {
            const std::string port_name(interner.resolve(pd.name));
            Port port;
            switch (pd.direction) {
                case bp2::Direction::Input:  port.direction = PortDirection::In; break;
                case bp2::Direction::Output: port.direction = PortDirection::Out; break;
                case bp2::Direction::InOut:  port.direction = PortDirection::InOut; break;
            }
            port.type = pd.port_type;
            port.domain = pd.domain;
            port.source_writer = false;
            dev.ports[port_name] = port;
        }

        // Params: convert float params to strings, filtering visual-only
        const TypeDefinition* type_def = type_registry ? type_registry->get(classname) : nullptr;
        auto is_visual_only = [&](const std::string& key) -> bool {
            if (!type_def) return false;
            auto it = type_def->param_schema.find(key);
            return it != type_def->param_schema.end() && it->second.visual_only;
        };
        auto is_int_param = [&](const std::string& key) -> bool {
            if (!type_def) return false;
            auto it = type_def->param_schema.find(key);
            return it != type_def->param_schema.end() && it->second.type == ParamSchemaType::Int;
        };

        for (const auto& [k, v] : comp.params) {
            std::string key(interner.resolve(k));
            if (is_visual_only(key)) continue;
            if (is_int_param(key)) {
                dev.params[key] = std::to_string(static_cast<long long>(v));
            } else {
                dev.params[key] = std::to_string(v);
            }
        }
        for (const auto& [k, v] : comp.string_params) {
            if (is_visual_only(k)) continue;
            dev.params[k] = v;
        }

        // Merge with type definition to get domains, execution metadata, solver_role, etc.
        if (type_def) {
            dev = merge_device_instance(dev, *type_def);
        }

        result.devices.push_back(std::move(dev));
    }

    // --- Build port_to_signal directly from FlatNetlist signal indices ---
    // Use component port_signals for the mapping: each entry maps
    // (component, port_name) → signal_index.
    //
    // We remap FlatNetlist signal indices to a compact contiguous range,
    // since merge_signals may leave gaps in the original index space.
    std::unordered_map<SignalIndex, uint32_t> signal_remap;
    uint32_t next_signal = 0;

    for (const auto& comp : netlist.components) {
        const std::string dev_id = node_id_from_path(comp.path, arena, interner);
        for (const auto& [port_iid, sig_idx] : comp.port_signals) {
            const std::string port_name(interner.resolve(port_iid));
            const std::string key = signal_key::make_node_port_key(dev_id, port_name);

            auto [it, inserted] = signal_remap.emplace(sig_idx, next_signal);
            if (inserted) {
                next_signal++;
            }
            result.port_to_signal[key] = it->second;
        }

        // Bridge nodes: also register the exposed key (parent_scope:bridge_name)
        const std::string classname(interner.resolve(comp.type));
        if (classname == "BlueprintInput" || classname == "BlueprintOutput") {
            const std::string exposed_key = signal_key::make_exposed_node_port_from_bridge_node(dev_id);
            if (!exposed_key.empty()) {
                // Find the ext port's signal to use for the exposed key
                for (const auto& [port_iid, sig_idx] : comp.port_signals) {
                    const std::string pn(interner.resolve(port_iid));
                    if (pn == "ext") {
                        auto [it, inserted] = signal_remap.emplace(sig_idx, next_signal);
                        if (inserted) {
                            next_signal++;
                        }
                        result.port_to_signal[exposed_key] = it->second;
                        break;
                    }
                }
            }
        }
    }

    result.signal_count = next_signal + 1; // +1 for sentinel

    return result;
}

} // namespace bp2::elaboration
