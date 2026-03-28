#include "fuel_tank.h"
#include "port_registry.h"
#include <algorithm>

template <typename Provider>
void FuelTank<Provider>::solve_hydraulic(SimulationState& st, float /*dt*/) {
    // Push model: output gravity head pressure based on fuel level
    float level_frac = level * inv_capacity;
    float gravity_pressure = density * 9.81f * level_frac;
    // Push: set flow_out to gravity pressure
    st.values[provider.get(PortNames::flow_out)] = gravity_pressure;
    // Output fuel level as a logical signal (0..1 fraction)
    st.values[provider.get(PortNames::level_out)] = level_frac;
}

template <typename Provider>
void FuelTank<Provider>::finalize_step(SimulationState& st, float dt) {
    float flow = st.values[provider.get(PortNames::flow_out)];
    float consumption = std::max(flow, 0.0f) * dt;
    level = std::max(level - consumption, 0.0f);
}

template <typename Provider>
void FuelTank<Provider>::execute(SimulationState& st, float dt) {
    solve_hydraulic(st, dt);
    finalize_step(st, dt);
}

template <typename Provider>
void FuelTank<Provider>::pre_load() {
    inv_capacity = 1.0f / std::max(capacity, 1e-6f);
    level = std::clamp(level, 0.0f, capacity);
}

template class FuelTank<JitProvider>;
