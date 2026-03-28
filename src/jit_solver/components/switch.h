#pragma once

#include "provider.h"
#include "../state.h"

/// Switch - manual toggle switch (triggered by control signal)
template <typename Provider = JitProvider>
class Switch {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    bool closed = false;
    float last_control = 0.0f;
    float downstream_g = 0.0f;
    float downstream_I = 0.0f;
    float v_out_old = 0.0f;

    Switch() = default;

    void solve_electrical(SimulationState& st, float dt);
    void commit_control(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
