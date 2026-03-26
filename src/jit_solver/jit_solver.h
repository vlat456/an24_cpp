#pragma once

#include "components/port_registry.h"
#include "scheduling.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

// DeviceInstance is defined in json_parser/json_parser.h
#include "../json_parser/json_parser.h"

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

/// Components grouped by explicit execution intent (Stage 4+).
///
/// Note: during migration this exists in parallel with DomainComponents.
/// Runtime may gradually switch from domain scheduling to phase scheduling.
struct PhaseComponents {
    std::vector<ComponentVariant*> electrical_passive;
    std::vector<ComponentVariant*> electrical_observer;
    std::vector<ComponentVariant*> logical;
    std::vector<ComponentVariant*> control_commit;
    std::vector<ComponentVariant*> electrical_actuator;
    std::vector<ComponentVariant*> finalize;
    std::vector<ComponentVariant*> mechanical;
    std::vector<ComponentVariant*> hydraulic;
    std::vector<ComponentVariant*> thermal;
};

/// Execution-phase capabilities (Stage 1 scaffolding).
///
/// These traits are metadata-only for now and do not affect scheduler behavior yet.
/// They are used to make phase intent explicit and to prepare JIT/AOT refactor work.
struct ExecutionTraits {
    bool electrical_passive = false;
    bool electrical_observer = false;
    bool logical = false;
    bool control_commit = false;
    bool electrical_actuator = false;
    bool finalize = false;
    bool mechanical = false;
    bool hydraulic = false;
    bool thermal = false;
};

/// Derive execution-phase metadata from component type (Stage 1, diagnostics only).
ExecutionTraits get_component_execution_traits(const ComponentVariant& variant);

/// Format execution traits for logging.
std::string get_execution_traits_string(const ExecutionTraits& t);

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

    /// Components sorted by execution phase intent (Stage 4 migration scaffold).
    PhaseComponents phase_components;

    /// LUT table arena - accumulated during build, moved to SimulationState at start
    std::vector<float> lut_keys;
    std::vector<float> lut_values;
};

BuildResult build_systems_dev(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
);
