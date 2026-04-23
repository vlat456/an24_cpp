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
    /// Initialize committed + staged state from initial_val param.
    void pre_load() {
        accumulator = next_accumulator = static_cast<double>(initial_val);
        first_frame_mask = 1.0f;
        next_first_frame_mask = 1.0f;
    }
};
