#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// TimeDelay - logic delay node with separate ON and OFF timers
template <typename Provider = JitProvider>
class TimeDelay {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float delay_on = 0.5f;
    float delay_off = 0.1f;

    // Committed state fields
    float accumulator = 0.0f;
    float current_out = 0.0f;
    float last_in = 0.0f;
    float first_frame_mask = 1.0f;

    // Staged next-state fields
    float next_accumulator = 0.0f;
    float next_current_out = 0.0f;
    float next_last_in = 0.0f;
    float next_first_frame_mask = 1.0f;

    TimeDelay() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
