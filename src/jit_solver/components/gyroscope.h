#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Gyroscope - power-only sensor
template <typename Provider = JitProvider>
class Gyroscope {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float conductance = 0.001f;

    Gyroscope() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load() {}
};
