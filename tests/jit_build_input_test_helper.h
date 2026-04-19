#pragma once

#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/aot/codegen_composite_helpers.h"
#include "json_parser/json_parser.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>

inline ResolvedDevice make_raw_resolved_device(
    std::string name,
    std::string classname,
    std::unordered_map<std::string, std::string> params,
    std::unordered_map<std::string, Port> ports,
    bool visual_only = false)
{
    ResolvedDevice dev;
    dev.name = std::move(name);
    dev.classname = std::move(classname);
    dev.params = std::move(params);
    dev.ports = std::move(ports);
    dev.visual_only = visual_only;
    return dev;
}

inline JitBuildInput make_jit_input_from_resolved(
    std::vector<ResolvedDevice> devices,
    std::unordered_map<std::string, float> initial_values = {})
{
    JitBuildInput input;
    input.devices = std::move(devices);
    input.initial_values = std::move(initial_values);

    uint32_t next_signal = 0;
    for (const auto& dev : input.devices) {
        if (dev.visual_only) {
            continue;
        }
        for (const auto& [port_name, port] : dev.ports) {
            (void)port;
            input.port_to_signal[dev.name + "." + port_name] = next_signal++;
        }
    }

    input.signal_count = next_signal + 1;
    return input;
}

/// Helper to construct JitBuildInput from explicit signal groups.
/// Each group is a vector of port strings that must map to the same signal.
/// Any declared device ports not mentioned in signal_groups are assigned their
/// own singleton signals so raw-builder tests keep the old "all ports exist"
/// behavior without relying on pairwise connection input.
inline JitBuildInput make_jit_input(
    std::vector<DeviceInstance> devices,
    const std::vector<std::vector<std::string>>& signal_groups,
    std::unordered_map<std::string, float> initial_values = {},
    const ComponentRegistry* registry = nullptr)
{
    JitBuildInput input;
    input.initial_values = std::move(initial_values);

    const ComponentRegistry& reg = registry ? *registry : load_component_registry("library/");

    input.devices.reserve(devices.size());
    for (const auto& dev : devices) {
        const ComponentSpec* spec = reg.get(dev.classname);
        if (spec == nullptr) {
            throw std::runtime_error("make_jit_input missing spec for device '" + dev.name +
                "' (classname: " + dev.classname + ")");
        }
        input.devices.push_back(resolve_component(dev, *spec));
    }
    
    std::unordered_set<std::string> seen_ports;
    
    // Assign each explicitly connected net first.
    for (size_t group_idx = 0; group_idx < signal_groups.size(); ++group_idx) {
        const auto& group = signal_groups[group_idx];
        for (const auto& port : group) {
            if (seen_ports.count(port)) {
                throw std::runtime_error("Duplicate port string across groups: " + port);
            }
            seen_ports.insert(port);
            input.port_to_signal[port] = static_cast<uint32_t>(group_idx);
        }
    }

    uint32_t next_signal = static_cast<uint32_t>(signal_groups.size());
    for (const auto& dev : input.devices) {
        if (dev.visual_only) {
            continue;
        }
        for (const auto& [port_name, port] : dev.ports) {
            (void)port;
            const std::string full_port = dev.name + "." + port_name;
            if (seen_ports.insert(full_port).second) {
                input.port_to_signal[full_port] = next_signal++;
            }
        }
    }

    input.signal_count = next_signal + 1;  // +1 for sentinel
    
    return input;
}

/// Helper to construct JitBuildInput from composite signal allocation rules.
/// Uses codegen_composite_detail production functions to compute signal allocation
/// from explicit device and connection pairs.
inline JitBuildInput make_jit_input_from_composite(
    std::vector<DeviceInstance> devices,
    const std::vector<BridgePortDefinition>& bridge_ports,
    const std::vector<Connection>& connections,
    const ComponentRegistry* registry = nullptr)
{
    JitBuildInput input;
    input.bridge_ports = bridge_ports;

    const ComponentRegistry& reg = registry ? *registry : load_component_registry("library/");

    input.devices.reserve(devices.size());
    for (const auto& dev : devices) {
        const ComponentSpec* spec = reg.get(dev.classname);
        if (spec == nullptr) {
            throw std::runtime_error("make_jit_input_from_composite missing spec for device '" + dev.name +
                "' (classname: " + dev.classname + ")");
        }
        input.devices.push_back(resolve_component(dev, *spec));
    }
    
    // Build port index map from all declared device ports
    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;
    codegen_composite_detail::build_port_index_map(input.devices, bridge_ports, all_ports, port_to_idx);
    
    // Construct union-find for signal allocation
    codegen_composite_detail::UnionFind uf(all_ports.size());
    
    // Apply signal allocation rules (connections, alias rules, etc.)
    codegen_composite_detail::apply_signal_allocation_rules(uf, input.devices, bridge_ports, connections, port_to_idx);
    
    // Finalize signal indices from union-find result
    uint32_t signal_count = 0;
    input.port_to_signal =
        codegen_composite_detail::finalize_signal_indices(uf, all_ports, port_to_idx, signal_count);
    
    input.signal_count = signal_count;
    
    return input;
}
