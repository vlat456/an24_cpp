#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Max - outputs the larger of two inputs
template <typename Provider = JitProvider>
class Max {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Max() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
