#pragma once

#include "components/port_registry.h"
#include "scheduler.h"
#include "subsolvers/subsolver_types.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <memory>
#include <vector>

// DeviceInstance is defined in json_parser/json_parser.h
#include "../json_parser/json_parser.h"

// Component forward declarations for SolverOwnedRefs typed pointers
#include "components/controlled_voltage_source.h"
#include "components/variable_conductance.h"
#include "components/azs.h"
#include "components/hold_button.h"
#include "components/relay.h"
#include "components/knob_switch.h"
#include "components/electrical_conductance.h"
#include "components/electrical_source.h"

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

/// Pre-built typed pointer lists for solver-owned components.
/// Populated at build time to eliminate per-frame std::visit scans
/// over the full 68-type ComponentVariant.
struct SolverOwnedRefs {
    // Dynamic source components (patched before solve_electrical each frame)
    std::vector<ControlledVoltageSource<JitProvider>*> controlled_voltage_sources;
    std::vector<VariableConductance<JitProvider>*> variable_conductances;
    std::vector<AZS<JitProvider>*> azs_switches;
    std::vector<HoldButton<JitProvider>*> hold_buttons;
    std::vector<Relay<JitProvider>*> relays;
    std::vector<KnobSwitch<JitProvider>*> knob_switches;

    // Commit-phase components (commit() called after solve_electrical each frame)
    std::vector<Generator<JitProvider>*> generators;
    std::vector<Resistor<JitProvider>*> resistors;
    std::vector<ElectricalConductance<JitProvider>*> electrical_conductances;
    std::vector<ElectricalSource<JitProvider>*> electrical_sources;
};

/// Build port-to-signal mapping from devices and connections
/// For AOT, this is used by codegen to generate component bindings
struct BuildResult {
    uint32_t signal_count;
    std::vector<uint32_t> fixed_signals;
    PortToSignal port_to_signal;

    /// Dynamic components for JIT mode (Editor).
    /// Map: device name -> ComponentVariant (type-safe storage container).
    /// NOTE: This is storage only — all per-frame dispatch uses PushScheduler
    /// (type-erased fn ptrs) or SolverOwnedRefs (typed pointer lists).
    /// std::visit on this map only happens at build time.
    std::unordered_map<std::string, ComponentVariant> devices;

    /// Push scheduler populated at build time.
    PushScheduler scheduler;

    /// Electrical network build plan (for subsolver)
    ElectricalBuildPlan electrical_plan;

    /// LUT table arena - accumulated during build, moved to SimulationState at start
    std::vector<float> lut_keys;
    std::vector<float> lut_values;

    /// Pre-built typed pointer lists for solver-owned components.
    /// Eliminates per-frame std::visit over all 68+ variant types.
    SolverOwnedRefs solver_owned;
};

BuildResult build_systems_dev(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
);
