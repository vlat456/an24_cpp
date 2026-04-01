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
    // Push actuator bridge: compute commanded differential voltage and apply it
    // relative to the currently solved/observed negative terminal.
    float cmd = st.values[provider.get(PortNames::cmd)];

    float v_source = std::clamp(cmd * gain + offset, min_v, max_v);
    float v_neg = st.values[provider.get(PortNames::v_neg)];

    // Keep v_neg externally driven; only drive the differential output.
    st.values[provider.get(PortNames::v_pos)] = v_neg + v_source;
}

template <typename Provider>
void ControlledVoltageSource<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class ControlledVoltageSource<JitProvider>;
