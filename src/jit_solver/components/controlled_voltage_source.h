#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"
#include "../subsolvers/subsolver_types.h"

/// ControlledVoltageSource - control-to-electrical bridge (Thevenin source)
/// Reads cmd (control scalar), outputs controlled voltage between v_pos and v_neg.
/// v_source = clamp(cmd * gain + offset, min_v, max_v)
///
/// Solver-owned: participates in the electrical solve as a TheveninSource element.
/// The source voltage is dynamic per-frame: before each solve, the simulator reads
/// the previous frame's cmd value from st.values[] (one-frame-delay semantic) and
/// patches the Thevenin voltage in the electrical plan.
template <typename Provider = JitProvider>
class ControlledVoltageSource {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    ElectricalPrimitiveHandle electrical_handle;
    float gain       = 1.0f;
    float offset     = 0.0f;
    float min_v      = 0.0f;
    float max_v      = 30.0f;
    float r_internal = 0.1f;
    float inv_r      = 10.0f;  // precomputed

    ControlledVoltageSource() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load();
};
