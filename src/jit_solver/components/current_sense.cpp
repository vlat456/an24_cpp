#include "current_sense.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void CurrentSense<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Prefer solved branch current when this sensor participates in the
    // electrical subsolver. For transitional push-actuated loops (for example
    // ControlledVoltageSource-driven circuits), fall back to local dV * G so
    // the sensor still reports meaningful current until the source is migrated
    // into the solver as well.
    float i_out = 0.0f;
    if (st.electrical_rt != nullptr) {
        i_out = get_branch_current(*st.electrical_rt, electrical_handle);
    }

    if (std::fabs(i_out) < 1e-9f) {
        float v_in = st.values[provider.get(PortNames::v_in)];
        float v_out = st.values[provider.get(PortNames::v_out)];
        i_out = (v_in - v_out) * conductance;
    }

    st.values[provider.get(PortNames::i_out)] = i_out;
}

template <typename Provider>
void CurrentSense<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class CurrentSense<JitProvider>;
