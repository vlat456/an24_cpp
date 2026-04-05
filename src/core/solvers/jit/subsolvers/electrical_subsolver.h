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

/// Backward-compatible overload.
/// Uses ElectricalRuntimeState::element_value_a as mutable runtime values,
/// initializing missing entries from plan defaults.
void solve_electrical(
    const ElectricalBuildPlan& plan,
    SimulationState& st,
    ElectricalRuntimeState& rt,
    double dt
) noexcept;
