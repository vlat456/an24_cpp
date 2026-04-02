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

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
