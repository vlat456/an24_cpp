#include "jit_solver.h"
#include "jit_solver_internal.h"
#include "../common/signal_union_rules.h"
#include "../common/signal_key.h"
#include "../../utils/union_find.h"
#include "io/json/parse_json_api.h"

#include <algorithm>
#include <map>
#include <vector>
#include <spdlog/spdlog.h>

using namespace jit_solver_impl;

/// Helper to compute port_to_signal mapping for JSON input.
/// Uses string-keyed port_to_idx internally (UnionFind operates on strings),
/// then converts to InternedId keys at the output boundary.
static void compute_signal_mapping(
    BuildResult& result,
    const std::vector<ResolvedDevice>& devices,
    const std::vector<BridgePortDefinition>& bridge_ports,
    const std::vector<std::pair<std::string, std::string>>& connections)
{
    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;

    for (const auto& dev : devices) {
         for (const auto& [port_name, port] : dev.ports) {
             (void)port;
             const std::string full_port = signal_key::make_node_port_key(dev.name, port_name);
             const uint32_t idx = static_cast<uint32_t>(all_ports.size());
             all_ports.push_back(full_port);
             port_to_idx[full_port] = idx;
         }

     }

    for (const auto& bridge : bridge_ports) {
        const std::string ext_key = signal_union_rules::bridge_external_key(bridge);
        const std::string port_key = signal_union_rules::bridge_internal_key(bridge);

        if (port_to_idx.count(ext_key) == 0) {
            const uint32_t idx = static_cast<uint32_t>(all_ports.size());
            all_ports.push_back(ext_key);
            port_to_idx[ext_key] = idx;
        }
        if (port_to_idx.count(port_key) == 0) {
            const uint32_t idx = static_cast<uint32_t>(all_ports.size());
            all_ports.push_back(port_key);
            port_to_idx[port_key] = idx;
        }

        const std::string exposed_key = signal_union_rules::bridge_exposed_key(bridge);
        if (!exposed_key.empty() && port_to_idx.count(exposed_key) == 0) {
            const uint32_t idx = static_cast<uint32_t>(all_ports.size());
            all_ports.push_back(exposed_key);
            port_to_idx[exposed_key] = idx;
        }
    }

    if (all_ports.empty()) {
        result.signal_count = 1; // sentinel
        return;
    }

    core::utils::UnionFind uf(all_ports.size());

    signal_union_rules::apply_structural_bridge_unions(uf, bridge_ports, port_to_idx);

    // === PARITY GUARD: bridge/connection/alias unions ===
    // INVARIANT: this must stay behaviorally aligned with the AOT path.
    signal_union_rules::apply_signal_union_rules(
        uf,
        devices,
        connections,
        port_to_idx,
        [](const std::string& from, const std::string& to, bool missing_from, bool missing_to) {
            if (missing_from) {
                spdlog::warn("[build] Connection references non-existent port '{}' (connected to '{}')", from, to);
            }
            if (missing_to) {
                spdlog::warn("[build] Connection references non-existent port '{}' (connected from '{}')", to, from);
            }
        });

    // UnionFind produces signal indices. Intern the string keys for typed output.
    std::map<uint32_t, uint32_t> root_to_signal;
    uint32_t next_signal = 0;
    for (const auto& [port_str, idx] : port_to_idx) {
        const uint32_t root = uf.find(idx);
        auto [it, inserted] = root_to_signal.emplace(root, next_signal);
        if (inserted) {
            next_signal++;
        }
        result.port_to_signal[result.signal_key_interner.intern(port_str)] = it->second;
    }

    result.signal_count = next_signal + 1; // sentinel at end
}

/// Shared build pipeline: phases 2–6 (component factory, electrical islands, etc.)
/// Assumes result.port_to_signal and result.signal_count are already populated.
static BuildResult build_from_signals(
    BuildResult result,
    const std::vector<ResolvedDevice>& devices
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
    result.signal_key_interner = input.signal_key_interner;
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

    std::vector<ResolvedDevice> devices = ctx.devices;

    // Compute port_to_signal mapping
    BuildResult temp_result{};
    compute_signal_mapping(temp_result, devices, ctx.bridge_ports, connections);

    // Return JitBuildInput with computed mapping and initial values
    return JitBuildInput{
        std::move(devices),
        ctx.bridge_ports,
        std::move(temp_result.port_to_signal),
        std::move(temp_result.signal_key_interner),
        temp_result.signal_count,
        ctx.initial_values
    };
}
