#pragma once

#include "provider.h"
#include "component_enums.h"
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

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
