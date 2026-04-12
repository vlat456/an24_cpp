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

std::string exposed_key_for_component(const FlatNetlist::Component& comp,
                                      std::string_view dev_id,
                                      const ui::StringInterner& interner) {
    const size_t sep = dev_id.rfind(':');
    if (sep == std::string_view::npos || sep == 0 || (sep + 1) >= dev_id.size()) {
        return "";
    }

    const std::string_view parent_instance = dev_id.substr(0, sep);
    if (!comp.exposed_port_name.empty()) {
        return signal_key::make_node_port_key(parent_instance, interner.resolve(comp.exposed_port_name));
    }

    const std::string_view bridge_segment = dev_id.substr(sep + 1);
    return signal_key::make_node_port_key(parent_instance, bridge_segment);
}

} // namespace

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
            const std::string exposed_key = exposed_key_for_component(comp, dev_id, interner);
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
