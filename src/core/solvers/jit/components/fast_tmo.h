#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// FastTMO - fast generalized Time Management Offset filter (low-pass)
template <typename Provider = JitProvider>
class FastTMO {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float tau = 0.1f;
    float inv_tau = 10.0f; // Precomputed
    float deadzone = 0.001f;

    // Committed state fields
    float current_value = 0.0f;
    float first_frame_mask = 1.0f; // Branchless init mask

    // Staged next-state fields
    float next_current_value = 0.0f;
    float next_first_frame_mask = 1.0f;

    FastTMO() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
