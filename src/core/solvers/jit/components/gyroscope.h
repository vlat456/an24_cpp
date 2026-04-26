#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// Gyroscope - power-only sensor
template <typename Provider = JitProvider>
class Gyroscope {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float conductance = 0.001f;

    Gyroscope() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
