#pragma once

#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"

/// OR - logical OR gate (o = A || B)
template <typename Provider = JitProvider>
class OR {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    OR() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
