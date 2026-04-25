#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// Lesser - outputs 1.0 if A < B, else 0.0
template <typename Provider = JitProvider>
class Lesser {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Lesser() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
