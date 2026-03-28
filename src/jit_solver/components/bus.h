#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Bus - electrical bus/rail, connects all ports together
template <typename Provider = JitProvider>
class Bus {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;

    Bus() = default;

    void solve_electrical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
