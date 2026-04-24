#pragma once

#include "core/solvers/common/provider.h"
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

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
