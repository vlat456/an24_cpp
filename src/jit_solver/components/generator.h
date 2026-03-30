#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"
#include "../subsolvers/subsolver_types.h"

/// Generator - voltage source like battery
template <typename Provider = JitProvider>
class Generator {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    ElectricalPrimitiveHandle electrical_handle;
    float internal_r = 0.005f;
    float inv_internal_r = 200.0f; // Precomputed
    float v_nominal = 28.5f;

    Generator() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load();
};
