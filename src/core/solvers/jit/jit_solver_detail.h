#pragma once

/// Internal types for JIT solver build and runtime.
/// NOT part of the public jit_solver.h API.
/// Only included by implementation files that need SolverOwnedRefs
/// (which depends on component headers for typed pointer lists).

#include "jit_solver.h"
#include "components/controlled_voltage_source.h"
#include "components/variable_conductance.h"
#include "components/azs.h"
#include "components/hold_button.h"
#include "components/relay.h"
#include "components/knob_switch.h"
#include "components/electrical_conductance.h"
#include "components/electrical_source.h"
#include "components/generator.h"
#include "components/resistor.h"
#include "core/solvers/common/provider.h"

#include <vector>

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
