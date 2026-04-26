#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// NOT - logical NOT gate (o = !A)
template <typename Provider = JitProvider>
class NOT {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    NOT() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
