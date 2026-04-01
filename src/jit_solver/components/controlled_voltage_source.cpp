#include "controlled_voltage_source.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void ControlledVoltageSource<Provider>::pre_load() {
    float safe_r = std::max(r_internal, 1e-9f);
    inv_r = 1.0f / safe_r;
}

/// Execute: no-op for solver-owned CVS.
/// Voltage propagation is handled by the electrical solver (TheveninSource element).
/// The dynamic source voltage is patched into the electrical plan before
/// solve_electrical() runs each frame (see update_dynamic_sources in simulator.cpp).
template <typename Provider>
void ControlledVoltageSource<Provider>::execute(SimulationState& /*st*/, float /*dt*/) {
    // Intentionally empty. Solver handles v_pos/v_neg via TheveninSource.
}

template <typename Provider>
void ControlledVoltageSource<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class ControlledVoltageSource<JitProvider>;
