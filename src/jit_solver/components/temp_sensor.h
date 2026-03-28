#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// TempSensor - temperature sensor
template <typename Provider = JitProvider>
class TempSensor {
public:
    static constexpr Domain domain = Domain::Thermal;

    Provider provider;
    float sensitivity = 1.0f;

    TempSensor() = default;

    void solve_thermal(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
