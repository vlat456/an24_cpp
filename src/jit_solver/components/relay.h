#pragma once

#include "provider.h"
#include "../state.h"

/// Relay - on/off switch controlled by voltage threshold
template <typename Provider = JitProvider>
class Relay {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    bool closed = false;
    float hold_threshold = 0.5f;

    Relay() = default;

    void commit_control(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
