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

    // Committed state fields
    double accumulator = 0.0;
    float first_frame_mask = 1.0f;

    // Staged next-state fields
    double next_accumulator = 0.0;
    float next_first_frame_mask = 1.0f;

    Integrator() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
