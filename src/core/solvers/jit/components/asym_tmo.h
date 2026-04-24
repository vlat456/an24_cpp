#pragma once

#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"

/// AsymTMO - asymmetric Time Management Offset filter (different rise/fall rates)
template <typename Provider = JitProvider>
class AsymTMO {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float tau_up = 0.1f;
    float tau_down = 0.5f;
    float inv_tau_up = 10.0f;
    float inv_tau_down = 2.0f;
    float deadzone = 0.001f;

    // Committed state fields
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    // Staged next-state fields
    float next_current_value = 0.0f;
    float next_first_frame_mask = 1.0f;

    AsymTMO() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
