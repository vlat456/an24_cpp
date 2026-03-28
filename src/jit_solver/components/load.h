#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Load - single port resistive load to ground
template <typename Provider = JitProvider>
class Load {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float conductance = 0.1f;

    Load() = default;

    void solve_electrical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
