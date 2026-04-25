#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// Inverter - DC to AC inverter
template <typename Provider = JitProvider>
class Inverter {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float efficiency = 0.95f;
    float frequency = 400.0f;

    Inverter() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
