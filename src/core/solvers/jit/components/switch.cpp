#include "switch.h"
#include "core/solvers/common/port_registry.h"
#include <cmath>

template <typename Provider>
void Switch<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Push model: when closed, propagate v_in to v_out; when open, set v_out=0
    if (closed) {
        float v_in = st.values[provider.get(PortNames::v_in)];
        st.values[provider.get(PortNames::v_out)] = v_in;
    } else {
        st.values[provider.get(PortNames::v_out)] = 0.0f;
    }
}

template <typename Provider>
void Switch<Provider>::commit(SimulationState& st, double /*dt*/) {
    float current_control = st.values[provider.get(PortNames::control)];

    if (std::abs(current_control - last_control) > 0.1f) {
        closed = !closed;
    }
    last_control = current_control;

    // Update state output
    st.values[provider.get(PortNames::state)] = closed ? 1.0f : 0.0f;
}

template class Switch<JitProvider>;
