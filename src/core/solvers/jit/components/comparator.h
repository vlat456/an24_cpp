#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// Comparator - voltage comparator with hysteresis
template <typename Provider = JitProvider>
class Comparator {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    bool output_state = false;
    float Von = 5.0f;
    float Voff = 2.0f;

    Comparator() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
