#include "electric_heater.h"
#include "port_registry.h"

template <typename Provider>
void ElectricHeater<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Push model: read power input
    // In push model, we don't have conductance stamps - just track the power
    // The electrical effect is implicit in the network
    (void)st.values[provider.get(PortNames::power)];

    // Thermal output: P_thermal = V^2 * G * efficiency
    // Where G = max_power / (V^2 + 0.01) for constant power behavior
    float v_in = st.values[provider.get(PortNames::power)];
    float v_sq = v_in * v_in;
    float g = max_power / (v_sq + 0.01f);
    float heat_power = v_sq * g * efficiency;
    st.values[provider.get(PortNames::heat_out)] = heat_power;
}

template <typename Provider>
void ElectricHeater<Provider>::commit(SimulationState& st) {
    (void)st;
}

template class ElectricHeater<JitProvider>;
