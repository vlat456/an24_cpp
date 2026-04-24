#include "sim_export.h"

#include "elaboration_detail.h"

#include "blueprint_v2/blueprint/blueprint.h"
#include "core/solvers/common/signal_key.h"
#include "core/solvers/jit/jit_solver.h"

#include <set>
#include <string>
#include <vector>

namespace bp2::elaboration {

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

std::vector<BridgePortDefinition> extract_bridge_definitions(
    const FlatNetlist& netlist,
    PathArena& arena,
    const ui::StringInterner& interner)
{
    std::vector<BridgePortDefinition> bridges;
    for (const auto& comp : netlist.components) {
        if (comp.exposed_port_name.empty()) continue;

        const std::string node_id = node_id_from_path(comp.path, arena, interner);

        // Determine direction and type from the "ext" port descriptor
        bp2::BridgeDirection dir = bp2::BridgeDirection::Input;
        PortType ptype = PortType::Signal;
        for (const auto& pd : comp.ports) {
            const std::string pname(interner.resolve(pd.name));
            if (pname == "ext") {
                dir = (pd.direction == bp2::Direction::Input)
                    ? bp2::BridgeDirection::Input
                    : bp2::BridgeDirection::Output;
                ptype = pd.port_type;
                break;
            }
        }

        BridgePortDefinition bridge;
        bridge.id = node_id;
        bridge.exposed_port = std::string(interner.resolve(comp.exposed_port_name));
        bridge.direction = dir;
        bridge.type = ptype;
        bridges.push_back(std::move(bridge));
    }
    return bridges;
}

std::string exposed_key_for_bridge(
    std::string_view bridge_dev_id,
    const ui::InternedId& exposed_port_name,
    const ui::StringInterner& interner)
{
    const size_t sep = bridge_dev_id.rfind(':');
    if (sep == std::string_view::npos || sep == 0 || (sep + 1) >= bridge_dev_id.size()) {
        return "";
    }
    const std::string_view parent_instance = bridge_dev_id.substr(0, sep);
    return signal_key::make_node_port_key(parent_instance, interner.resolve(exposed_port_name));
}

// ==================================================================
// elaborate_for_jit — direct FlatNetlist → JitBuildInput (no JSON)
// ==================================================================

JitBuildInput elaborate_for_jit(
    const FlatNetlist& netlist,
    PathArena& arena,
    const ui::StringInterner& interner,
    const ComponentRegistry& type_registry) {

    JitBuildInput result;

    // --- Phase 1: Build resolved devices from flat components ---
    std::set<std::string> emitted_ids;
    for (const auto& comp : netlist.components) {
        const std::string dev_id = node_id_from_path(comp.path, arena, interner);
        if (!emitted_ids.insert(dev_id).second) continue;

        // Bridge nodes are structural — not simulation devices
        if (!comp.exposed_port_name.empty()) continue;

        const std::string classname(interner.resolve(comp.type));
        const ComponentSpec* type_def = type_registry.get(classname);
        if (!type_def) {
            throw std::runtime_error("Component definition not found: " + classname);
        }

        auto dev = detail::build_resolved_device(
            comp, dev_id, classname, *type_def, interner, type_registry,
            /*fill_defaults=*/false);
        if (!dev.has_value()) continue;

        result.devices.push_back(std::move(*dev));
    }

    // --- Phase 2: Build port_to_signal from FlatNetlist signal indices ---
    // compact_signals() already produces dense contiguous indices,
    // so signal indices are used directly — no remap needed.
    //
    // Keys are interned via signal_key_interner — string construction happens
    // once at build time, runtime lookups use InternedId (integer comparison).
    uint32_t next_signal = 0;

    for (const auto& comp : netlist.components) {
        const std::string dev_id = node_id_from_path(comp.path, arena, interner);
        for (const auto& [port_iid, sig_idx] : comp.port_signals) {
            const std::string port_name(interner.resolve(port_iid));
            const std::string key = signal_key::make_node_port_key(dev_id, port_name);

            if (sig_idx >= next_signal) next_signal = sig_idx + 1;
            result.port_to_signal[result.signal_key_interner.intern(key)] = sig_idx;
        }

        // Structural bridge nodes: expose the parent-facing key
        if (!comp.exposed_port_name.empty()) {
            const std::string exposed_key = exposed_key_for_bridge(dev_id, comp.exposed_port_name, interner);
            if (!exposed_key.empty()) {
                for (const auto& [port_iid, sig_idx] : comp.port_signals) {
                    if (interner.resolve(port_iid) == "ext") {
                        if (sig_idx >= next_signal) next_signal = sig_idx + 1;
                        result.port_to_signal[result.signal_key_interner.intern(exposed_key)] = sig_idx;
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
