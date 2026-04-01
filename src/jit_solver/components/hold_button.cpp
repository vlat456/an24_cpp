#include "hold_button.h"
#include "port_registry.h"
#include <cmath>

template <typename Provider>
void HoldButton<Provider>::commit_control(SimulationState& st, float dt) {
    (void)dt;
    float current_control = st.values[provider.get(PortNames::control)];
    bool active = std::abs(current_control - idle) > 0.1f;
    is_pressed = active;

    // Update state output
    st.values[provider.get(PortNames::state)] = is_pressed ? 1.0f : 0.0f;
}

template <typename Provider>
void HoldButton<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Electrical behavior is solver-owned via dynamic conductance branch.
    // HoldButton execute only updates non-electrical control/state outputs.
    (void)st;
}

template <typename Provider>
void HoldButton<Provider>::commit(SimulationState& st, float /*dt*/) {
    commit_control(st, 0.0f);
}

template class HoldButton<JitProvider>;
