#pragma once

#include "hydraulic_subsolver_types.h"
#include "../state.h"

/// Solve hydraulic network for one timestep.
///
/// Implements nodal analysis with conductance stamping for islands.
/// Handles FixedPressureNode, PressureSource (Norton equivalent),
/// and FlowBranch elements. Writes solved pressures to
/// SimulationState::values and branch flows to HydraulicRuntimeState.
void solve_hydraulic(
    const HydraulicBuildPlan& plan,
    const std::vector<float>& element_value_a,
    SimulationState& st,
    HydraulicRuntimeState& rt,
    double dt
) noexcept;

/// Self-contained overload that initializes element_value_a from plan defaults
/// on first call and reuses the buffer across frames.
void solve_hydraulic(
    const HydraulicBuildPlan& plan,
    SimulationState& st,
    HydraulicRuntimeState& rt,
    double dt
) noexcept;
