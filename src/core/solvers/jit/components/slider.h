#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// Slider - interactive value source (control -> out passthrough)
template <typename Provider = JitProvider>
class Slider {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    float min = 0.0f;
    float max = 1.0f;

    Slider() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
