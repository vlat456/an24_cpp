#include "radiator.h"
#include "port_registry.h"

template <typename Provider>
void Radiator<Provider>::solve_thermal(SimulationState& st, float /*dt*/) {
    // Push model: heat exchanger
    // Heat flows from heat_in to heat_out, with cooling capacity
    float heat_in = st.values[provider.get(PortNames::heat_in)];
    // Apply cooling: scale = 1 / (1 + cooling_capacity * 0.001)
    // This keeps output in (0, heat_in] for any positive cooling_capacity
    float heat_out = heat_in / (1.0f + cooling_capacity * 0.001f);
    st.values[provider.get(PortNames::heat_out)] = heat_out;
}

template <typename Provider>
void Radiator<Provider>::execute(SimulationState& st, float dt) {
    solve_thermal(st, dt);
}

template class Radiator<JitProvider>;
