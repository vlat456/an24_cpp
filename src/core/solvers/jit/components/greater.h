#pragma once

#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"

/// Greater - outputs 1.0 if A > B, else 0.0
template <typename Provider = JitProvider>
class Greater {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Greater() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
