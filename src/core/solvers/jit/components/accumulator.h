#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Accumulator - pure stateful signal accumulator: state += in * dt
/// Minimal primitive for building composite filters (e.g. FirstOrderLag).
/// Uses two-phase semantics: execute reads committed state, commit advances.
template <typename Provider = JitProvider>
class Accumulator {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float initial_val = 0.0f;

    // Committed state fields
    double state = 0.0;
    float first_frame_mask = 1.0f;

    // Staged next-state fields
    double next_state = 0.0;
    float next_first_frame_mask = 1.0f;

    Accumulator() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
