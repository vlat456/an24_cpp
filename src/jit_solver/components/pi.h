#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// PI - Proportional-Integral controller
template <typename Provider = JitProvider>
class PI {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    float Kp = 1.0f;
    float Ki = 0.0f;
    float output_min = -1000.0f;
    float output_max = 1000.0f;

    // State variables (minimal footprint: 1 float, no derivative)
    double integral = 0.0;

    PI() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
