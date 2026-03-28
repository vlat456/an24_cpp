#pragma once

#include "provider.h"
#include "component_enums.h"
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
    float current_value = 0.0f;
    float first_frame_mask = 1.0f; // Branchless init mask

    FastTMO() = default;

    void solve_logical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load();
};
