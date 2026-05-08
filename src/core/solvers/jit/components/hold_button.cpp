#include "hold_button.h"
#include "core/solvers/common/port_names.h"
#include <cmath>

template <typename Provider>
void HoldButton<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Electrical behavior is solver-owned via dynamic conductance branch.
    // HoldButton execute only updates non-electrical control/state outputs.
    (void)st;
}

template <typename Provider>
void HoldButton<Provider>::commit(SimulationState& st, double /*dt*/) {
    float const current_control = st.values[provider.get(PortNames::control)];
    bool const active = std::abs(current_control - idle) > 0.1f;
    is_pressed = active;

    // Update state output
    st.values[provider.get(PortNames::state)] = is_pressed ? 1.0f : 0.0f;
}

template class HoldButton<JitProvider>;
