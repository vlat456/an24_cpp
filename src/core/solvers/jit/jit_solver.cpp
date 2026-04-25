#include "jit_solver.h"
#include "jit_solver_internal.h"
#include "core/solvers/common/signal_allocation.h"
#include "io/json/parse_json_api.h"

#include <spdlog/spdlog.h>

using namespace jit_solver_impl;

/// Special member functions defined here where SolverOwnedRefs is a complete type
/// (via jit_solver_internal.h → jit_solver_detail.h).
BuildResult::~BuildResult() = default;
BuildResult::BuildResult() = default;
BuildResult::BuildResult(BuildResult&&) noexcept = default;
BuildResult& BuildResult::operator=(BuildResult&&) noexcept = default;

/// Compute port_to_signal mapping for JSON input using the shared signal_alloc pipeline.
static void compute_signal_mapping(
    BuildResult& result,
    const std::vector<ResolvedDevice>& devices,
    const std::vector<BridgePortDefinition>& bridge_ports,
    const std::vector<Connection>& connections)
{
    // Phase 1: enumerate all ports
    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;
    signal_alloc::build_port_index_map(devices, bridge_ports, all_ports, port_to_idx);

    if (all_ports.empty()) {
        result.signal_count = 1; // sentinel only
        return;
    }

    // Phase 2: apply union rules (with spdlog warnings for missing ports)
    signal_alloc::UnionFind uf(all_ports.size());
    signal_alloc::apply_signal_allocation_rules(
        uf, devices, bridge_ports, connections, port_to_idx,
        [](const std::string& from, const std::string& to, bool missing_from, bool missing_to) {
            if (missing_from) {
                spdlog::warn("[build] Connection references non-existent port '{}' (connected to '{}')", from, to);
            }
            if (missing_to) {
                spdlog::warn("[build] Connection references non-existent port '{}' (connected from '{}')", to, from);
            }
        });

    // Phase 3: compact UnionFind roots to dense signal indices
    uint32_t signal_count = 0;
    auto string_p2s = signal_alloc::finalize_signal_indices(uf, all_ports, port_to_idx, signal_count);

    // Intern the string keys for typed output
    for (const auto& [port_str, sig] : string_p2s) {
        result.port_to_signal[result.signal_key_interner.intern(port_str)] = sig;
    }

    result.signal_count = signal_count;
}

/// Shared build pipeline: phases 2–6 (component factory, electrical islands, etc.)
/// Assumes result.port_to_signal and result.signal_count are already populated.
static BuildResult build_from_signals(
    BuildResult result,
    const std::vector<ResolvedDevice>& devices
) {
    // Create solver_owned here to ensure it's always valid (even for empty circuits)
    // so tests can safely check solver_owned->empty().
    result.solver_owned = std::make_unique<SolverOwnedRefs>();

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
    result.signal_key_interner = input.signal_key_interner;
    result.signal_count = input.signal_count;
    return build_from_signals(std::move(result), input.devices);
}

JitBuildInput build_input_from_json(const std::string& json_str) {
    auto ctx = parse_json(json_str);

    std::vector<ResolvedDevice> devices = ctx.devices;

    // Compute port_to_signal mapping via shared signal_alloc pipeline
    BuildResult temp_result{};
    compute_signal_mapping(temp_result, devices, ctx.bridge_ports, ctx.connections);

    return JitBuildInput{
        std::move(devices),
        std::move(ctx.bridge_ports),
        std::move(temp_result.port_to_signal),
        std::move(temp_result.signal_key_interner),
        temp_result.signal_count,
        std::move(ctx.initial_values)
    };
}
