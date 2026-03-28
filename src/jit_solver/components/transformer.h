#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Transformer - AC transformer with voltage ratio
template <typename Provider = JitProvider>
class Transformer {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float ratio = 1.0f;

    Transformer() = default;

    void solve_electrical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
