#pragma once

#include "component.h"
#include "systems.h"
#include "state.h"
#include <string>
#include <unordered_map>
#include <memory>

// DeviceInstance is defined in json_parser/json_parser.h
// which is included transitively through component.h -> systems.h

namespace an24 {

/// Port-to-signal mapping
using PortToSignal = std::unordered_map<std::string, uint32_t>;

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

/// Create a single component from device instance (for testing/validation)
std::unique_ptr<Component> create_component(
    const DeviceInstance& device,
    const PortToSignal& port_to_signal,
    uint32_t signal_count
);

} // namespace an24
