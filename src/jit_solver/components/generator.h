#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Generator - voltage source like battery
template <typename Provider = JitProvider>
class Generator {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float internal_r = 0.005f;
    float inv_internal_r = 200.0f; // Precomputed
    float v_nominal = 28.5f;

    Generator() = default;

    void solve_electrical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load();
};
