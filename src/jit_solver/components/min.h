#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Min - outputs the smaller of two inputs
template <typename Provider = JitProvider>
class Min {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Min() = default;

    void solve_logical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
