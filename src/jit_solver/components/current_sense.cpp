#include "current_sense.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void CurrentSense<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Push model: CurrentSense is a measurement-only component
    // Compute current from voltage difference and conductance
    float v_in = st.values[provider.get(PortNames::v_in)];
    float v_out = st.values[provider.get(PortNames::v_out)];
    float v_diff = v_in - v_out;
    st.values[provider.get(PortNames::i_out)] = v_diff * conductance;
}

template <typename Provider>
void CurrentSense<Provider>::execute(SimulationState& st, float dt) {
    solve_electrical(st, dt);
}

template class CurrentSense<JitProvider>;
