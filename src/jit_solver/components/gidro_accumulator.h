#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// GidroAccumulator - gas-charged hydraulic accumulator (Boyle's law)
/// Stores hydraulic energy via compressed gas. When system pressure exceeds
/// precharge, fluid enters and gas compresses. When pressure drops, stored
/// fluid is released back into the circuit.
template <typename Provider = JitProvider>
class GidroAccumulator {
public:
    static constexpr Domain domain = Domain::Hydraulic;

    Provider provider;
    float precharge_pressure = 50.0f;  // Gas precharge pressure (psi)
    float volume = 10.0f;              // Total accumulator volume (liters)
    float gas_volume = 10.0f;          // Current gas volume (liters, state)

    GidroAccumulator() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load();
};
