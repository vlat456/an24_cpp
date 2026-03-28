#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// RefNode - fixed voltage reference
template <typename Provider = JitProvider>
class RefNode {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float value = 0.0f;

    RefNode() = default;

    void solve_electrical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
