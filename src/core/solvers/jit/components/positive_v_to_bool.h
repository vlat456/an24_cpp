#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// Positive_V_to_Bool - convert positive voltage to TRUE (v > 0)
template <typename Provider = JitProvider>
class Positive_V_to_Bool {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Positive_V_to_Bool() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
