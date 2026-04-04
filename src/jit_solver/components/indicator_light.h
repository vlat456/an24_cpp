#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"
#include "../subsolvers/subsolver_types.h"
#include <string>

/// IndicatorLight - aircraft indicator light
template <typename Provider = JitProvider>
class IndicatorLight {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    ElectricalPrimitiveHandle electrical_handle;
    float conductance = 0.002f;  // ~1.5W indicator light (Soviet СМ28-1.5): R = V²/P ≈ 523Ω, G ≈ 0.002S
    float rated_voltage = 28.0f;
    float inv_rated_voltage = 1.0f / 28.0f; // precomputed

    IndicatorLight() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
