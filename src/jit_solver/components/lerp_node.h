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
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    LerpNode() = default;

    void solve_electrical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void finalize_step(SimulationState& st, float dt);
    void pre_load() {}
};
