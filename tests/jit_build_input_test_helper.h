#pragma once

#include "core/solvers/jit/jit_solver.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>

/// Helper to construct JitBuildInput from explicit signal groups.
/// Each group is a vector of port strings that must map to the same signal.
/// Any declared device ports not mentioned in signal_groups are assigned their
/// own singleton signals so raw-builder tests keep the old "all ports exist"
/// behavior without relying on pairwise connection input.
inline JitBuildInput make_jit_input(
    std::vector<DeviceInstance> devices,
    const std::vector<std::vector<std::string>>& signal_groups,
    std::unordered_map<std::string, float> initial_values = {})
{
    JitBuildInput input;
    input.devices = std::move(devices);
    input.initial_values = std::move(initial_values);
    
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
