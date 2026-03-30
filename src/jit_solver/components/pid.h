#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// PID - Proportional-Integral-Derivative controller
template <typename Provider = JitProvider>
class PID {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    float Kp = 1.0f;
    float Ki = 0.0f;
    float Kd = 0.0f;
    float output_min = -1000.0f;
    float output_max = 1000.0f;
    float filter_alpha = 0.2f;

    // State variables (minimal footprint: 3 floats)
    float integral = 0.0f;
    float last_error = 0.0f;
    float d_filtered = 0.0f;

    PID() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
