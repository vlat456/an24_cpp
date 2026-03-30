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

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load() {}
};
