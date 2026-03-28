#include "relay.h"
#include "port_registry.h"

template <typename Provider>
void Relay<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Push model: when closed, propagate v_in to v_out; when open, set v_out=0
    // Control logic handled in commit_control
    if (closed) {
        float v_in = st.values[provider.get(PortNames::v_in)];
        st.values[provider.get(PortNames::v_out)] = v_in;
    } else {
        st.values[provider.get(PortNames::v_out)] = 0.0f;
    }
}

template <typename Provider>
void Relay<Provider>::commit_control(SimulationState& st, float dt) {
    (void)dt;
    float control = st.values[provider.get(PortNames::control)];
    closed = control > hold_threshold;
}

template <typename Provider>
void Relay<Provider>::execute(SimulationState& st, float dt) {
    commit_control(st, dt);
    solve_electrical(st, dt);
}

template class Relay<JitProvider>;
