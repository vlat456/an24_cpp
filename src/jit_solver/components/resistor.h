#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Resistor - pure conductance element
template <typename Provider = JitProvider>
class Resistor {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float conductance = 0.1f;

    Resistor() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load() {}
};
