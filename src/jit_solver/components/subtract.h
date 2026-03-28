#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Subtract - subtractor (o = A - B)
template <typename Provider = JitProvider>
class Subtract {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Subtract() = default;

    void solve_logical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
