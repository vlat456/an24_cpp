#pragma once

#include "nodal_types.h"
#include "../state.h"

/// Solve a nodal network for one timestep.
///
/// Domain-agnostic: works for electrical (voltage/current), hydraulic
/// (pressure/flow), or any domain using the three-element nodal model
/// (FixedNode, Source, Branch).
///
/// Writes solved potentials to SimulationState::values and branch flows
/// to NodalRuntimeState::branch_flows.
///
/// @param plan              Build plan with islands and elements
/// @param element_value_a   Dynamic source values (patched each frame)
/// @param st                Simulation state (potentials written back)
/// @param rt                Runtime scratch buffers (branch flows, diagnostics)
/// @param dt                Time step (clamped by caller)
void solve_nodal(
    const NodalBuildPlan& plan,
    const std::vector<float>& element_value_a,
    SimulationState& st,
    NodalRuntimeState& rt,
    double dt
) noexcept;

/// Self-contained overload that initializes element_value_a from plan defaults.
/// Used by AOT codegen and standalone tests. The JIT Simulator uses the 5-arg
/// version with explicit element_value_a after start-time initialization.
void solve_nodal(
    const NodalBuildPlan& plan,
    SimulationState& st,
    NodalRuntimeState& rt,
    double dt
) noexcept;
