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

    Switch() = default;

    void commit_control(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
