#include "fuel_tank.h"
#include "core/solvers/common/port_names.h"
#include <algorithm>

template <typename Provider>
void FuelTank<Provider>::execute(SimulationState& st, double dt) {
    float level_frac = static_cast<float>(level * inv_capacity);

    // Write normalized level to level_out (for gauges/instruments).
    st.values[provider.get(PortNames::level_out)] = level_frac;

    // Compute gravity pressure and write to p_source signal.
    // The CopySignal patch op reads this and copies to element_value_a
    // (PressureSource P_th) for the next frame's hydraulic solve.
    float gravity_pressure = density * GRAVITY * tank_height * level_frac;
    st.values[provider.get(PortNames::p_source)] = gravity_pressure;

    // Compute next fuel level from consumption.
    double consumption = std::max(static_cast<double>(consumption_rate), 0.0) * dt;
    next_level = std::max(level - consumption, 0.0);
}

template <typename Provider>
void FuelTank<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
    level = next_level;
}

template <typename Provider>
void FuelTank<Provider>::pre_load() {
    inv_capacity = 1.0f / std::max(capacity, 1e-6f);
    level = std::clamp(level, 0.0, static_cast<double>(capacity));
    next_level = level;
}

template class FuelTank<JitProvider>;
