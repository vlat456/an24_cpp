#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// PI - Proportional-Integral controller
template <typename Provider = JitProvider>
class PI {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    // State variables (minimal footprint: 1 float, no derivative)
    double integral = 0.0;

    PI() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
