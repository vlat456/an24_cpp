#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Clamp - clamps input value between min and max
template <typename Provider = JitProvider>
class Clamp {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    float min = 0.0f;
    float max = 1.0f;

    Clamp() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
