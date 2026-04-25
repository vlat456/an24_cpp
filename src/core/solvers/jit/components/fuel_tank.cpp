#include "fuel_tank.h"
#include "core/solvers/common/port_names.h"
#include <algorithm>

template <typename Provider>
void FuelTank<Provider>::execute(SimulationState& st, double dt) {
    float level_frac = level * inv_capacity;
    float gravity_pressure = density * 9.81f * level_frac;
    st.values[provider.get(PortNames::flow_out)] = gravity_pressure;
    st.values[provider.get(PortNames::level_out)] = level_frac;

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
