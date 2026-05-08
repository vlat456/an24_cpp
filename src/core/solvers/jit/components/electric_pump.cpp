#include "electric_pump.h"
#include "core/solvers/common/port_names.h"

template <typename Provider>
void ElectricPump<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Push model: electrical side - read inputs but don't stamp conductance
    // Just track the power consumption for informational purposes
    float const v_in = st.values[provider.get(PortNames::v_in)];
    (void)v_in;

    // Push model: hydraulic side - pump pressure boost
    float const p_in_h = st.values[provider.get(PortNames::p_in)];
    // Target pressure boost proportional to input voltage
    float const target_p = v_in * max_pressure / 28.0f;
    // Push: set output pressure = input + boost
    float const p_out = p_in_h + target_p;
    st.values[provider.get(PortNames::p_out)] = p_out;
}

template <typename Provider>
void ElectricPump<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class ElectricPump<JitProvider>;
