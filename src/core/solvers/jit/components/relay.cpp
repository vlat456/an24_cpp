#include "relay.h"
#include "core/solvers/common/port_registry.h"

template <typename Provider>
void Relay<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Electrical behavior is solver-owned via dynamic conductance branch.
    // Relay execute only updates non-electrical control/state outputs.
    (void)st;
}

template <typename Provider>
void Relay<Provider>::commit(SimulationState& st, double /*dt*/) {
    float control = st.values[provider.get(PortNames::control)];
    float ht = st.values[provider.get(PortNames::hold_threshold)];

    if (control > ht) {
        closed = true;
    } else if (control < -ht) {
        closed = false;
    }

    st.values[provider.get(PortNames::state)] = closed ? 1.0f : 0.0f;
}

template class Relay<JitProvider>;
