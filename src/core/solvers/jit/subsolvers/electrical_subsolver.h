#pragma once

#include "subsolver_types.h"
#include "../state.h"

/// Solve electrical network for one timestep.
///
/// Implements nodal analysis with conductance stamping for islands.
/// Handles FixedVoltageNode, TheveninSource (Norton equivalent),
/// and ConductanceBranch elements. Writes solved voltages to
/// SimulationState::values and branch currents to ElectricalRuntimeState.
void solve_electrical(
    const ElectricalBuildPlan& plan,
    const std::vector<float>& element_value_a,
    SimulationState& st,
    ElectricalRuntimeState& rt,
    double dt
) noexcept;

/// Self-contained overload that initializes element_value_a from plan defaults.
/// Used by AOT codegen and standalone tests. The JIT Simulator uses the 5-arg
/// version with explicit element_value_a after start-time initialization.
void solve_electrical(
    const ElectricalBuildPlan& plan,
    SimulationState& st,
    ElectricalRuntimeState& rt,
    double dt
) noexcept;
