#include "relay.h"
#include "port_registry.h"

template <typename Provider>
void Relay<Provider>::commit_control(SimulationState& st, double dt) {
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
void Relay<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Electrical behavior is solver-owned via dynamic conductance branch.
    // Relay execute only updates non-electrical control/state outputs.
    (void)st;
}

template <typename Provider>
void Relay<Provider>::commit(SimulationState& st, double /*dt*/) {
    commit_control(st, 0.0f);
}

template class Relay<JitProvider>;
