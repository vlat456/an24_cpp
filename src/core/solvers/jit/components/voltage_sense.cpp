#include "voltage_sense.h"
#include "core/solvers/common/port_names.h"
#include "../state.h"

template <typename Provider>
void VoltageSense<Provider>::execute(SimulationState& st, double /*dt*/) {
    float const v = st.values[provider.get(PortNames::v_in)];
    float const vref = st.values[provider.get(PortNames::v_ref)];
    float const g = st.values[provider.get(PortNames::gain)];
    float const ofs = st.values[provider.get(PortNames::offset)];
    st.values[provider.get(PortNames::out)] = (v - vref) * g + ofs;
}

template <typename Provider>
void VoltageSense<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class VoltageSense<JitProvider>;
