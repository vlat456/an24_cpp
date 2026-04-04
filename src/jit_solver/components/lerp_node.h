#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// LerpNode - linear interpolation with deadzone
template <typename Provider = JitProvider>
class LerpNode {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    float factor = 1.0f;
    float deadzone = 0.001f;

    // Committed state fields
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    // Staged next-state fields
    float next_current_value = 0.0f;
    float next_first_frame_mask = 1.0f;

    LerpNode() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
