#include "inverter.h"
#include "core/solvers/common/port_names.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void Inverter<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Push model: DC to AC conversion
    // Input: dc_in, Output: ac_out
    float const v_dc = st.values[provider.get(PortNames::dc_in)];
    // AC output is DC input scaled by efficiency
    float const v_ac_target = v_dc * efficiency;
    st.values[provider.get(PortNames::ac_out)] = v_ac_target;
}

template <typename Provider>
void Inverter<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Inverter<JitProvider>;
