#include "jit_solver.h"
#include "jit_solver_internal.h"

#include <algorithm>
#include <vector>

using namespace jit_solver_impl;

/// Shared build pipeline: phases 2–6 (component factory, electrical islands, etc.)
/// Assumes result.port_to_signal and result.signal_count are already populated.
static BuildResult build_from_signals(
    BuildResult result,
    const std::vector<DeviceInstance>& devices
) {
    if (result.signal_count <= 1) {
        // Empty system, sentinel only
        result.fixed_signals.push_back(0);
        result.devices.seal();
        return result;
    }

    // Phase 2: Component factory and scheduler registration
    build_and_register_components(result, devices);

    // Phase 3: Electrical island extraction and handle assignment
    build_electrical_islands(result, devices);

    // Phase 4: Populate solver-owned typed reference lists
    populate_solver_owned_refs(result);

    // Phase 5: Build compiled electrical runtime patch operations
    build_electrical_patch_ops(result);

    // Phase 6: Build compiled solver-owned step operations (execute + commit)
    build_solver_step_ops(result);

    // Freeze component storage after all pointer extraction is complete.
    // Prevents accidental post-build structural mutation that would invalidate
    // cached scheduler/solver pointers.
    result.devices.seal();

    return result;
}

BuildResult build_systems_dev(const JitBuildInput& input) {
    BuildResult result{};
    result.port_to_signal = input.port_to_signal;
    result.signal_count = input.signal_count;
    return build_from_signals(std::move(result), input.devices);
}

JitBuildInput build_input_from_json(const std::string& json_str) {
    // Parse JSON to get devices and connections
    auto ctx = parse_json(json_str);

    // Convert to connection pairs
    std::vector<std::pair<std::string, std::string>> connections;
    connections.reserve(ctx.connections.size());
    for (const auto& c : ctx.connections) {
        connections.push_back({c.from, c.to});
    }

    // Build a temporary BuildResult just to compute port_to_signal
    BuildResult temp_result{};
    process_port_unions(temp_result, ctx.devices, connections);

    // Return JitBuildInput with computed mapping and initial values
    return JitBuildInput{
        ctx.devices,
        temp_result.port_to_signal,
        temp_result.signal_count,
        ctx.initial_values  // Include initial values from JSON
    };
}
