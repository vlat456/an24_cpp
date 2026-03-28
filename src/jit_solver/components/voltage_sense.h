#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// VoltageSense - electrical-to-control bridge (observer only)
/// Reads (v_in - v_ref) after the electrical solve, outputs a scalar control signal.
/// Does NOT stamp conductance - purely observes, never drives the network.
/// Domain: Electrical (to read ports after SOR) + Logical (to write output)
template <typename Provider = JitProvider>
class VoltageSense {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Logical;

    Provider provider;
    float gain   = 1.0f;
    float offset = 0.0f;

    VoltageSense() = default;

    void solve_electrical(SimulationState& st, float dt);  // no-op stamp, output written here
    void solve_logical(SimulationState& st, float dt);     // writes out = (v_in - v_ref) * gain + offset
    void observe_electrical(SimulationState& st, float dt); // Stage 2 hook shim
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
