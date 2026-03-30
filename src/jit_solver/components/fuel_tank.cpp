#include "fuel_tank.h"
#include "port_registry.h"
#include <algorithm>

template <typename Provider>
void FuelTank<Provider>::execute(SimulationState& st, float dt) {
    float level_frac = level * inv_capacity;
    float gravity_pressure = density * 9.81f * level_frac;
    st.values[provider.get(PortNames::flow_out)] = gravity_pressure;
    st.values[provider.get(PortNames::level_out)] = level_frac;

    float consumption = std::max(consumption_rate, 0.0f) * dt;
    next_level = std::max(level - consumption, 0.0f);
}

template <typename Provider>
void FuelTank<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
    level = next_level;
}

template <typename Provider>
void FuelTank<Provider>::pre_load() {
    inv_capacity = 1.0f / std::max(capacity, 1e-6f);
    level = std::clamp(level, 0.0f, capacity);
    next_level = level;
}

template class FuelTank<JitProvider>;
