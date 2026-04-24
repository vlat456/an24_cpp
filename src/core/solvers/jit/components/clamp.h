#pragma once

#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"

/// Clamp - clamps input value between min and max
template <typename Provider = JitProvider>
class Clamp {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Clamp() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
