#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"
#include "../subsolvers/subsolver_types.h"

/// HoldButton - hold-to-operate button with press/release detection
template <typename Provider = JitProvider>
class HoldButton {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    ElectricalPrimitiveHandle electrical_handle;
    float idle = 0.0f;
    float g_open = 1e-6f;
    float g_closed = 1000.0f;
    bool is_pressed = false;

    HoldButton() = default;

    void commit_control(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load() {}
};
