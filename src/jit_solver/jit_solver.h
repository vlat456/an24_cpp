#pragma once

#include "components/port_registry.h"
#include "scheduling.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

// DeviceInstance is defined in json_parser/json_parser.h
#include "../json_parser/json_parser.h"

namespace an24 {

// Forward declarations
struct SimulationState;

/// Port-to-signal mapping
using PortToSignal = std::unordered_map<std::string, uint32_t>;

/// Component pointers sorted by domain for zero-branch iteration
struct DomainComponents {
    std::vector<ComponentVariant*> electrical;  // 60 Hz (every step)
    std::vector<ComponentVariant*> logical;     // 60 Hz (every step)
    std::vector<ComponentVariant*> mechanical;  // 20 Hz (every 3rd step)
    std::vector<ComponentVariant*> hydraulic;   // 5 Hz (every 12th step)
    std::vector<ComponentVariant*> thermal;     // 1 Hz (every 60th step)
};

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

    /// Components sorted by domain for data-oriented iteration
    /// This enables zero-branch scheduling: just iterate the right domain's vector
    /// Components with multiple solve methods appear in multiple domain vectors
    DomainComponents domain_components;
};

BuildResult build_systems_dev(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
);

} // namespace an24
