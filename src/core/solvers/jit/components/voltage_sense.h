#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// VoltageSense - electrical-to-control bridge (observer only)
/// Reads (v_in - v_ref) after the electrical solve, outputs a scalar control signal.
/// Does NOT stamp conductance - purely observes, never drives the network.
/// Domain: Electrical (to read ports after solve) + Logical (to write output)
template <typename Provider = JitProvider>
class VoltageSense {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Logical;

    Provider provider;

    VoltageSense() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
