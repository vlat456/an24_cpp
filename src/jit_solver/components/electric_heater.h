#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// ElectricHeater - electrical heater
template <typename Provider = JitProvider>
class ElectricHeater {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Thermal;

    Provider provider;
    float max_power = 1000.0f;
    float efficiency = 0.9f;

    ElectricHeater() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}

};
