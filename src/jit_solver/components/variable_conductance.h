#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// VariableConductance - control-to-electrical bridge (variable resistor)
/// Reads cmd [0..1], computes g = lerp(g_min, g_max, cmd) between v_in and v_out.
/// Models command-controlled winding resistance, field effect, or variable load.
/// Push model: simple pass-through / attenuator behavior from v_in to v_out.
template <typename Provider = JitProvider>
class VariableConductance {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float g_min = 0.001f;
    float g_max = 10.0f;

    VariableConductance() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
