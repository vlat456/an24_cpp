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
    float downstream_g = 0.0f;
    float downstream_I = 0.0f;
    float v_out_old = 0.0f;

    Relay() = default;

    void solve_electrical(SimulationState& st, float dt);
    void commit_control(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
