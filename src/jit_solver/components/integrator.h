#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Integrator - mathematical integrator with reset: out = integral(in * dt)
template <typename Provider = JitProvider>
class Integrator {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float gain = 1.0f;
    float initial_val = 0.0f;
    float accumulator = 0.0f;
    float first_frame_mask = 1.0f;

    Integrator() = default;

    void solve_logical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
