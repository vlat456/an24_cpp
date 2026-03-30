#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Lesser - outputs 1.0 if A < B, else 0.0
template <typename Provider = JitProvider>
class Lesser {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Lesser() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
