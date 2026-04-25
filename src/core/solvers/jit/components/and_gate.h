#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// AND - logical AND gate (o = A && B)
template <typename Provider = JitProvider>
class AND {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    AND() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
