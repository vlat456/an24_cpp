#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Monostable - pulse timer (one-shot): outputs 1.0 for duration after rising edge
template <typename Provider = JitProvider>
class Monostable {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float duration = 30.0f;
    float timer = 0.0f;
    float last_in = 0.0f;

    Monostable() = default;

    void solve_logical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
