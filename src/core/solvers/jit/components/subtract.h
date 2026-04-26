#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// Subtract - subtractor (o = A - B)
template <typename Provider = JitProvider>
class Subtract {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Subtract() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
