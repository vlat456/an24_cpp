#pragma once

#include "components/port_registry.h"
#include "scheduler.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <memory>
#include <vector>

// DeviceInstance is defined in json_parser/json_parser.h
#include "../json_parser/json_parser.h"

// Forward declarations
struct SimulationState;

/// Port-to-signal mapping
using PortToSignal = std::unordered_map<std::string, uint32_t>;

/// Get domain bitmask from component (reads static constexpr Domain field)
inline Domain get_component_domain_mask(const ComponentVariant& variant) {
    return std::visit([](auto& comp) -> Domain {
        using CompType = std::decay_t<decltype(comp)>;
        return CompType::domain;
    }, variant);
}

/// Build port-to-signal mapping from devices and connections
/// For AOT, this is used by codegen to generate component bindings
struct BuildResult {
    uint32_t signal_count;
    std::vector<uint32_t> fixed_signals;
    PortToSignal port_to_signal;

    /// Dynamic components for JIT mode (Editor)
    /// Map: device name -> ComponentVariant (type-safe dynamic container)
    std::unordered_map<std::string, ComponentVariant> devices;

    /// Push scheduler populated at build time.
    PushScheduler scheduler;

    /// LUT table arena - accumulated during build, moved to SimulationState at start
    std::vector<float> lut_keys;
    std::vector<float> lut_values;
};

BuildResult build_systems_dev(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
);
