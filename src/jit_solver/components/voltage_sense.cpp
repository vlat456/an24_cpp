#include "voltage_sense.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void VoltageSense<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Pure observer behavior: out = (v_in - v_ref) * gain + offset
    float v = st.values[provider.get(PortNames::v_in)];
    float vref = st.values[provider.get(PortNames::v_ref)];
    st.values[provider.get(PortNames::out)] = (v - vref) * gain + offset;
}

template <typename Provider>
void VoltageSense<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class VoltageSense<JitProvider>;
