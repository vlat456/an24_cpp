#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// Merger - 2-to-1 signal merger (inverse of Splitter)
template <typename Provider = JitProvider>
class Merger {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Merger() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
