#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// SlewRate - linear rate of change limiter (slew rate limiter)
template <typename Provider = JitProvider>
class SlewRate {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float max_rate = 1.0f;
    float deadzone = 0.0001f;

    // Committed state fields
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    // Staged next-state fields
    float next_current_value = 0.0f;
    float next_first_frame_mask = 1.0f;

    SlewRate() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
