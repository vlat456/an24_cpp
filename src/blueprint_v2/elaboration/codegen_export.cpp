#include "codegen_export.h"
#include "sim_export.h"       // node_id_from_path, exposed_key_for_bridge
#include "elaboration_detail.h"  // build_resolved_device

#include "core/solvers/common/signal_key.h"

#include <set>
#include <string>

namespace bp2::elaboration {

// ==================================================================
// elaborate_for_codegen — FlatNetlist → CodegenBuildInput
//
// Produces string-keyed port_to_signal for the codegen pipeline.
// Single source of truth for device resolution is in elaboration_detail.h.
// ==================================================================

CodegenBuildInput elaborate_for_codegen(
    const FlatNetlist& netlist,
    PathArena& arena,
    const ui::StringInterner& interner,
    const ComponentRegistry& type_registry)
{
    CodegenBuildInput result;

    // --- Phase 1: Build resolved devices from flat components ---
    // Codegen needs fill_defaults=true (complete param set for generated code).
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
            /*fill_defaults=*/true);
        if (!dev.has_value()) continue;

        result.devices.push_back(std::move(*dev));
    }

    // --- Phase 2: Build port_to_signal from FlatNetlist signal indices ---
    // compact_signals() already produced dense contiguous indices.
    // Keys are plain strings matching codegen's "node_id.port_name" convention.
    uint32_t next_signal = 0;

    for (const auto& comp : netlist.components) {
        const std::string dev_id = node_id_from_path(comp.path, arena, interner);

        for (const auto& [port_iid, sig_idx] : comp.port_signals) {
            const std::string port_name(interner.resolve(port_iid));
            const std::string key = signal_key::make_node_port_key(dev_id, port_name);

            if (sig_idx >= next_signal) next_signal = sig_idx + 1;
            result.port_to_signal[key] = sig_idx;
        }

        // Bridge nodes: expose the parent-facing key pointing to the ext signal
        if (!comp.exposed_port_name.empty()) {
            const std::string exposed_key = exposed_key_for_bridge(dev_id, comp.exposed_port_name, interner);
            if (!exposed_key.empty()) {
                for (const auto& [port_iid, sig_idx] : comp.port_signals) {
                    if (interner.resolve(port_iid) == "ext") {
                        if (sig_idx >= next_signal) next_signal = sig_idx + 1;
                        result.port_to_signal[exposed_key] = sig_idx;
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
