#include "controlled_voltage_source.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void ControlledVoltageSource<Provider>::pre_load() {
    float safe_r = std::max(r_internal, 1e-9f);
    inv_r = 1.0f / safe_r;
}

/// Execute method for scheduler integration
template <typename Provider>
void ControlledVoltageSource<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Push model implementation:
    // Reads cmd control input, computes source voltage with gain/offset/limits,
    // then sets v_pos to the computed voltage and v_neg to 0 (ground reference).
    // Read control input
    float cmd = st.values[provider.get(PortNames::cmd)];
    
    // Compute controlled voltage with gain, offset, and limits
    float v_source = std::clamp(cmd * gain + offset, min_v, max_v);
    
    // Push model: set output pins directly
    // v_pos = controlled voltage, v_neg = 0 (ground reference)
    st.values[provider.get(PortNames::v_pos)] = v_source;
    st.values[provider.get(PortNames::v_neg)] = 0.0f;
}

template <typename Provider>
void ControlledVoltageSource<Provider>::commit(SimulationState& st) {
    (void)st;
}

template class ControlledVoltageSource<JitProvider>;

