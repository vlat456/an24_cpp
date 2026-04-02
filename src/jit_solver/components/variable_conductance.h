#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"
#include "../subsolvers/subsolver_types.h"

/// VariableConductance - control-to-electrical bridge (variable resistor)
/// Reads cmd [0..1], computes g = lerp(g_min, g_max, cmd) between v_in and v_out.
/// Models command-controlled winding resistance, field effect, or variable load.
///
/// Solver-owned: participates in the electrical solve as a ConductanceBranch element.
/// The conductance is dynamic per-frame: before each solve, the simulator reads
/// the previous frame's cmd value from st.values[] (one-frame-delay semantic) and
/// patches the conductance in the electrical plan.
template <typename Provider = JitProvider>
class VariableConductance {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    ElectricalPrimitiveHandle electrical_handle;
    float g_min = 0.001f;
    float g_max = 10.0f;

    VariableConductance() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
