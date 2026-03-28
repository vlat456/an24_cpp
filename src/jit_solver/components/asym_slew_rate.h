#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// AsymSlewRate - asymmetric linear rate limiter (different rise/fall rates)
template <typename Provider = JitProvider>
class AsymSlewRate {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float rate_up = 1.0f;
    float rate_down = 0.5f;
    float deadzone = 0.0001f;
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    AsymSlewRate() = default;

    void solve_logical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
