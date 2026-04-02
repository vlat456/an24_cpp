#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"
#include "../subsolvers/subsolver_types.h"

/// Relay - on/off switch controlled by voltage threshold
template <typename Provider = JitProvider>
class Relay {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    ElectricalPrimitiveHandle electrical_handle;
    bool closed = false;
    float hold_threshold = 0.5f;
    float g_open = 1e-6f;
    float g_closed = 1000.0f;

    Relay() = default;

    void commit_control(SimulationState& st, double dt);
    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
