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
    SimulationState& st,
    ElectricalRuntimeState& rt,
    float dt
);