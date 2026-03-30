#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// HoldButton - hold-to-operate button with press/release detection
template <typename Provider = JitProvider>
class HoldButton {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float idle = 0.0f;
    bool is_pressed = false;

    HoldButton() = default;

    void commit_control(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load() {}
};
