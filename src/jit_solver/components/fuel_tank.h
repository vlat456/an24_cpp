#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// FuelTank - aircraft fuel reservoir with gravity head pressure
/// Provides fuel flow proportional to fuel level. Level decreases
/// as flow is consumed by downstream components.
template <typename Provider = JitProvider>
class FuelTank {
public:
    static constexpr Domain domain = Domain::Hydraulic;

    Provider provider;
    float capacity = 1000.0f;   // Tank capacity (liters)
    float level = 1000.0f;      // Current fuel amount (liters, state)
    float density = 0.78f;      // Fuel density (kg/L, kerosene TS-1)
    float inv_capacity = 0.001f; // Precomputed 1/capacity

    FuelTank() = default;

    void solve_hydraulic(SimulationState& st, float dt);
    void finalize_step(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load();
};
