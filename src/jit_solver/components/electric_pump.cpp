#include "electric_pump.h"
#include "port_registry.h"

template <typename Provider>
void ElectricPump<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Push model: electrical side - read inputs but don't stamp conductance
    // Just track the power consumption for informational purposes
    float v_in = st.values[provider.get(PortNames::v_in)];
    (void)v_in;

    // Push model: hydraulic side - pump pressure boost
    float p_in_h = st.values[provider.get(PortNames::p_in)];
    // Target pressure boost proportional to input voltage
    float target_p = v_in * max_pressure / 28.0f;
    // Push: set output pressure = input + boost
    float p_out = p_in_h + target_p;
    st.values[provider.get(PortNames::p_out)] = p_out;
}

template <typename Provider>
void ElectricPump<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class ElectricPump<JitProvider>;
