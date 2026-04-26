#include "sim_export.h"

#include "elaboration_detail.h"

#include "core/solvers/common/signal_key.h"

#include <set>
#include <string>
#include <vector>

namespace bp2::elaboration {

std::vector<BridgePortDefinition> extract_bridge_definitions(
    const FlatNetlist& netlist,
    PathArena& arena,
    const core::StringInterner& interner)
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

// ==================================================================
// elaborate_for_jit — direct FlatNetlist → JitBuildInput (no JSON)
// ==================================================================

JitBuildInput elaborate_for_jit(
    const FlatNetlist& netlist,
    PathArena& arena,
    const core::StringInterner& interner,
    const ComponentRegistry& type_registry) {

    JitBuildInput result;

    // --- Phase 1: Build resolved devices (fill_defaults=false for JIT) ---
    auto collected = detail::collect_devices(netlist, arena, interner, type_registry,
                                             /*fill_defaults=*/false);
    // Convert to solver-facing view — strips editor-only fields.
    result.devices.reserve(collected.devices.size());
    for (auto& rd : collected.devices) {
        result.devices.push_back(to_solver_device(rd));
    }

    // --- Phase 2: Build port_to_signal with InternedId keys ---
    auto sig_result = detail::collect_port_signals(netlist, arena, interner,
        [&](const std::string& key, uint32_t sig_idx) {
            result.port_to_signal[result.signal_key_interner.intern(key)] = sig_idx;
        });

    result.signal_count = sig_result.next_signal + 1; // +1 for sentinel

    return result;
}

} // namespace bp2::elaboration
