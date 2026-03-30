#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// AGK47 - attitude gyro
template <typename Provider = JitProvider>
class AGK47 {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float conductance = 0.001f;

    AGK47() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load() {}
};
