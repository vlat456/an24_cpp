#include "high_power_load.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void HighPowerLoad<Provider>::execute(SimulationState& st, float /*dt*/) {
    // TODO(Phase 3): Implement power-balance model.
    // Currently a no-op stub — power_draw and min_voltage_diff are unused.
    // Push model needs current injection or voltage drop to model load behavior.
    (void)st;
}

template <typename Provider>
void HighPowerLoad<Provider>::commit(SimulationState& st) {
    (void)st;
}

template class HighPowerLoad<JitProvider>;
