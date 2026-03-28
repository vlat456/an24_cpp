#include "hold_button.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void HoldButton<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Push model: when pressed, propagate v_in to v_out; when not pressed, set v_out=0
    // Control logic handled in commit_control
    if (is_pressed) {
        float v_in = st.values[provider.get(PortNames::v_in)];
        st.values[provider.get(PortNames::v_out)] = v_in;
    } else {
        st.values[provider.get(PortNames::v_out)] = 0.0f;
    }
}

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
void HoldButton<Provider>::execute(SimulationState& st, float dt) {
    commit_control(st, dt);
    solve_electrical(st, dt);
}

template class HoldButton<JitProvider>;
