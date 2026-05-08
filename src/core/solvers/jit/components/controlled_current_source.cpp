#include "controlled_current_source.h"
#include "core/solvers/common/port_names.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void ControlledCurrentSource<Provider>::pre_load() {
    // Precompute r_shunt from g_shunt
    r_shunt = (g_shunt > 1e-9f) ? (1.0f / g_shunt) : 1e9f;
}

/// Execute method for scheduler integration
template <typename Provider>
void ControlledCurrentSource<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Push model implementation:
    // Reads cmd control input, computes source current with gain/limits,
    // emulates current source effect via voltage offset (V = I * r_shunt).
    // This is a deterministic approximation since push model cannot inject current.
    // Read control input
    float const cmd = st.values[provider.get(PortNames::cmd)];
    
    // Compute commanded current with gain and limits
    float const i_source = std::clamp(cmd * gain, min_i, max_i);
    
    // Push model: emulate current source effect via voltage
    // Set v_pos to the voltage equivalent of the current (V = I * r_shunt)
    // and v_neg to 0 as reference
    float const v_equivalent = i_source * r_shunt;
    st.values[provider.get(PortNames::v_pos)] = v_equivalent;
    st.values[provider.get(PortNames::v_neg)] = 0.0f;
}

template <typename Provider>
void ControlledCurrentSource<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class ControlledCurrentSource<JitProvider>;

