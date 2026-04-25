#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// Min - outputs the smaller of two inputs
template <typename Provider = JitProvider>
class Min {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Min() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
