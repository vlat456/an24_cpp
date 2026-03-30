#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// PD - Proportional-Derivative controller
template <typename Provider = JitProvider>
class PD {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    float Kp = 1.0f;
    float Kd = 0.0f;
    float output_min = -1000.0f;
    float output_max = 1000.0f;
    float filter_alpha = 0.2f;

    // State variables (minimal footprint: 2 floats, no integral)
    float last_error = 0.0f;
    float d_filtered = 0.0f;

    PD() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load() {}
};
