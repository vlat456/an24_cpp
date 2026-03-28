#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// ControlledVoltageSource - control-to-electrical bridge (Thevenin source)
/// Reads cmd (control scalar), outputs controlled voltage between v_pos and v_neg.
/// v_source = clamp(cmd * gain + offset, min_v, max_v)
/// Push model: sets v_pos = v_source, v_neg = 0
template <typename Provider = JitProvider>
class ControlledVoltageSource {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float gain       = 1.0f;
    float offset     = 0.0f;
    float min_v      = 0.0f;
    float max_v      = 30.0f;
    float r_internal = 0.1f;
    float inv_r      = 10.0f;  // precomputed

    ControlledVoltageSource() = default;

    void solve_electrical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load();
};

