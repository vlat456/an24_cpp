#include "codegen_export.h"
#include "elaboration_utils.h"   // node_id_from_path, exposed_key_for_bridge
#include "elaboration_detail.h"  // build_resolved_device, collect_devices, collect_port_signals
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
    core::StringInterner& interner,
    const ComponentRegistry& type_registry)
{
    CodegenBuildInput result;

    // --- Phase 1: Build resolved devices (fill_defaults=true for codegen) ---
    auto collected = detail::collect_devices(netlist, arena, interner, type_registry,
                                             /*fill_defaults=*/true);
    result.devices = std::move(collected.devices);

    // --- Phase 2: Build port_to_signal with plain string keys ---
    auto sig_result = detail::collect_port_signals(netlist, arena, interner,
        [&](const std::string& key, uint32_t sig_idx) {
            result.port_to_signal[key] = sig_idx;
        });

    result.signal_count = sig_result.next_signal + 1; // +1 for sentinel

    return result;
}

} // namespace bp2::elaboration
