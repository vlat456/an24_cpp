#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Merger - 2-to-1 signal merger (inverse of Splitter)
template <typename Provider = JitProvider>
class Merger {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Merger() = default;

    void solve_electrical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void solve_mechanical(SimulationState& st, float dt);
    void solve_hydraulic(SimulationState& st, float dt);
    void solve_thermal(SimulationState& st, float dt);
    void pre_load() {}
};
