#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// FuelTank - aircraft fuel reservoir with gravity head pressure.
/// Exposes pressure head and normalized level. Fuel level decreases
/// by configured constant consumption_rate each simulation step.
template <typename Provider = JitProvider>
class FuelTank {
public:
    static constexpr Domain domain = Domain::Hydraulic;

    Provider provider;
    float capacity = 1000.0f;   // Tank capacity (liters)
    float level = 1000.0f;      // Current fuel amount (liters, state)
    float next_level = 1000.0f; // Next-frame fuel amount (liters)
    float density = 0.78f;      // Fuel density (kg/L, kerosene TS-1)
    float consumption_rate = 0.0f; // Constant fuel draw (liters/second)
    float inv_capacity = 0.001f; // Precomputed 1/capacity

    FuelTank() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load();
};
