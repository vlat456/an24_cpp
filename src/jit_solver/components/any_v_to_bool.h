#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Any_V_to_Bool - convert any non-zero voltage to TRUE (including negative)
template <typename Provider = JitProvider>
class Any_V_to_Bool {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Any_V_to_Bool() = default;

    void solve_logical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
