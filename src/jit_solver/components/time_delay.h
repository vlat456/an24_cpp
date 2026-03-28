#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// TimeDelay - logic delay node with separate ON and OFF timers
template <typename Provider = JitProvider>
class TimeDelay {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float delay_on = 0.5f;
    float delay_off = 0.1f;

    float accumulator = 0.0f;
    float current_out = 0.0f;
    float last_in = 0.0f;
    float first_frame_mask = 1.0f;

    TimeDelay() = default;

    void solve_logical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
