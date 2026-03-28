#pragma once

#include "provider.h"
#include "../state.h"

/// Battery - voltage source with internal resistance
template <typename Provider = JitProvider>
class Battery {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float capacity = 1000.0f;
    float charge = 1000.0f;
    float internal_r = 0.01f;
    float inv_internal_r = 100.0f;
    float v_nominal = 28.0f;

    Battery() = default;

    void solve_electrical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load();
};
