#pragma once

#include "components/port_registry.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

// DeviceInstance is defined in json_parser/json_parser.h
#include "../json_parser/json_parser.h"

namespace an24 {

/// Port-to-signal mapping
using PortToSignal = std::unordered_map<std::string, uint32_t>;

/// Build port-to-signal mapping from devices and connections
/// For AOT, this is used by codegen to generate component bindings
struct BuildResult {
    uint32_t signal_count;
    std::vector<uint32_t> fixed_signals;
    PortToSignal port_to_signal;

    /// Dynamic components for JIT mode (Editor)
    /// Map: device name -> ComponentVariant (type-safe dynamic container)
    std::unordered_map<std::string, ComponentVariant> devices;
};

BuildResult build_systems_dev(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
);

} // namespace an24
