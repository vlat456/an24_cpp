#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Multiply - multiplier (o = A * B)
template <typename Provider = JitProvider>
class Multiply {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Multiply() = default;

    void solve_logical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
