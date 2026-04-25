#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// P - Proportional controller
template <typename Provider = JitProvider>
class P {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    float Kp = 1.0f;
    float output_min = -1000.0f;
    float output_max = 1000.0f;

    // No state variables (pure memoryless function)

    P() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
