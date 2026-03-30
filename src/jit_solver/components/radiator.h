#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Radiator - heat exchanger
template <typename Provider = JitProvider>
class Radiator {
public:
    static constexpr Domain domain = Domain::Thermal;

    Provider provider;
    float cooling_capacity = 1000.0f;

    Radiator() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
