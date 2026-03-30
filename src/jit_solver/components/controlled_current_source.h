#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// ControlledCurrentSource - control-to-electrical bridge (Norton current source)
/// Reads cmd (control scalar), stamps commanded current into the network.
/// i_source = clamp(cmd * gain, min_i, max_i)
/// Push model: emulates current source effect via voltage offset based on shunt resistance.
/// Since push model cannot inject current directly, we emulate by setting voltage
/// across v_pos/v_neg proportional to the commanded current (V = I * r_shunt).
template <typename Provider = JitProvider>
class ControlledCurrentSource {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float gain    = 1.0f;
    float min_i   = 0.0f;
    float max_i   = 100.0f;
    float g_shunt = 0.001f;  // small parallel conductance for well-conditioning
    float r_shunt = 1000.0f; // precomputed: 1.0f / g_shunt

    ControlledCurrentSource() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load();
};
