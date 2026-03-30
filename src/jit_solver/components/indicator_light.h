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
    float max_brightness = 100.0f;
    float conductance = 1.0f;  // low resistance pass-through indicator
    float rated_voltage = 28.0f;
    float inv_rated_voltage = 1.0f / 28.0f; // precomputed
    std::string color = "white";

    IndicatorLight() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load();
};
