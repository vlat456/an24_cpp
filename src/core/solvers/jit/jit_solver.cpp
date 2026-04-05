#include "jit_solver.h"
#include "jit_solver_internal.h"

#include <algorithm>
#include <vector>

using namespace jit_solver_impl;

BuildResult build_systems_dev(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
) {
    BuildResult result{};

    // Phase 1: Signal allocation and port union-find
    process_port_unions(result, devices, connections);

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
