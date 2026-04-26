#pragma once

#include "hydraulic_subsolver_types.h"
#include "../state.h"

/// Solve hydraulic network for one timestep.
///
/// Implements nodal analysis with conductance stamping for islands.
/// Handles FixedPressureNode, PressureSource (Norton equivalent),
/// and FlowBranch elements. Writes solved pressures to
/// SimulationState::values and branch flows to HydraulicRuntimeState.
///
/// Caller must initialize rt.element_value_a before calling (e.g., via
/// build_common::init_element_values_from_plan).
void solve_hydraulic(
    const HydraulicBuildPlan& plan,
    const std::vector<float>& element_value_a,
    SimulationState& st,
    HydraulicRuntimeState& rt,
    double dt
) noexcept;
