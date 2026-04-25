#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// XOR - logical XOR gate (o = A != B)
template <typename Provider = JitProvider>
class XOR {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    XOR() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
