#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Greater - outputs 1.0 if A > B, else 0.0
template <typename Provider = JitProvider>
class Greater {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Greater() = default;

    void solve_logical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
