#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// GreaterEq - outputs 1.0 if A >= B, else 0.0
template <typename Provider = JitProvider>
class GreaterEq {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    GreaterEq() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
