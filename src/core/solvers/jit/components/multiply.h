#pragma once

#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"

/// Multiply - multiplier (o = A * B)
template <typename Provider = JitProvider>
class Multiply {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Multiply() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
