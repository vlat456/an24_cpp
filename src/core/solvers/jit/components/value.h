#pragma once

#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"

/// Value - generic constant output for math/logical/control graphs.
/// Replaces misuse of RefNode as a scalar constant source.
/// Unlike RefNode (which is an electrical fixed-voltage node for the solver),
/// Value has no electrical semantics and is never extracted into the
/// electrical plan.
template <typename Provider = JitProvider>
class Value {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    float value = 0.0f;

    Value() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
