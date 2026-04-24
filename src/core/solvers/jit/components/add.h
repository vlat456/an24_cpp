#pragma once

#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"

/// Add - adder (o = A + B)
template <typename Provider = JitProvider>
class Add {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Add() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
