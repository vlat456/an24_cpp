#include "relay.h"
#include "port_registry.h"

template <typename Provider>
void Relay<Provider>::commit_control(SimulationState& st, float dt) {
    (void)dt;
    float control = st.values[provider.get(PortNames::control)];

    if (control > hold_threshold) {
        closed = true;
    } else if (control < -hold_threshold) {
        closed = false;
    }

    st.values[provider.get(PortNames::state)] = closed ? 1.0f : 0.0f;
}

template <typename Provider>
void Relay<Provider>::execute(SimulationState& st, float /*dt*/) {
    if (closed) {
        float v_in = st.values[provider.get(PortNames::v_in)];
        st.values[provider.get(PortNames::v_out)] = v_in;
    } else {
        st.values[provider.get(PortNames::v_out)] = 0.0f;
    }
}

template <typename Provider>
void Relay<Provider>::commit(SimulationState& st, float /*dt*/) {
    commit_control(st, 0.0f);
}

template class Relay<JitProvider>;