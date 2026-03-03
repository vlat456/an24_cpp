#pragma once

#include "systems.h"
#include "state.h"
#include <string>
#include <unordered_map>

namespace an24 {

/// Device instance from JSON/KDL
struct DeviceInstance {
    std::string name;
    std::string internal;  // "Battery", "Relay", "RefNode", etc.
    std::unordered_map<std::string, std::string> params;
    std::unordered_map<std::string, std::string> ports;
};

/// Port-to-signal mapping
using PortToSignal = std::unordered_map<std::string, uint32_t>;

/// Create component from DeviceInstance
std::unique_ptr<Component> create_component(
    const DeviceInstance& device,
    const PortToSignal& port_to_signal,
    uint32_t signal_count
);

/// Build complete systems from devices and connections
/// Returns: systems, signal_count, fixed_signals, port_to_signal
struct BuildResult {
    Systems systems;
    uint32_t signal_count;
    std::vector<uint32_t> fixed_signals;
    PortToSignal port_to_signal;
};

BuildResult build_systems_dev(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
);

} // namespace an24
