#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// Radiator - heat exchanger
template <typename Provider = JitProvider>
class Radiator {
public:
    static constexpr Domain domain = Domain::Thermal;

    Provider provider;
    float cooling_capacity = 1000.0f;

    Radiator() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
