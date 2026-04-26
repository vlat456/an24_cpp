#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// TempSensor - temperature sensor
template <typename Provider = JitProvider>
class TempSensor {
public:
    static constexpr Domain domain = Domain::Thermal;

    Provider provider;
    float sensitivity = 1.0f;

    TempSensor() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
