#pragma once

#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"

/// LesserEq - outputs 1.0 if A <= B, else 0.0
template <typename Provider = JitProvider>
class LesserEq {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    LesserEq() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
