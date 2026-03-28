#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// SlewRate - linear rate of change limiter (slew rate limiter)
template <typename Provider = JitProvider>
class SlewRate {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float max_rate = 1.0f;
    float deadzone = 0.0001f;
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    SlewRate() = default;

    void solve_logical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
