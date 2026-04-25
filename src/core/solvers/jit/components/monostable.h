#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// Monostable - pulse timer (one-shot): outputs 1.0 for duration after rising edge
template <typename Provider = JitProvider>
class Monostable {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float duration = 30.0f;

    // Committed state fields
    float timer = 0.0f;
    float last_in = 0.0f;

    // Staged next-state fields
    float next_timer = 0.0f;
    float next_last_in = 0.0f;

    Monostable() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
