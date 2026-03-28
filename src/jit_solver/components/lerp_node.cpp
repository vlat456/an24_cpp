#include "lerp_node.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void LerpNode<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Push model: pass-through element
    float v_input = st.values[provider.get(PortNames::input)];
    st.values[provider.get(PortNames::output)] = v_input;
}

template <typename Provider>
void LerpNode<Provider>::execute(SimulationState& st, float dt) {
    // For lerp_node, we need to compute in finalize_step
    // This is a placeholder that does nothing - actual work happens in finalize_step
    (void)st;
    (void)dt;
}

template <typename Provider>
void LerpNode<Provider>::finalize_step(SimulationState& st, float dt) {
    (void)dt;
    float v_input = st.values[provider.get(PortNames::input)];

    current_value += (v_input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    float diff = v_input - current_value;
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    float new_output = current_value + factor * diff * dz_mask;
    current_value = new_output;
    st.values[provider.get(PortNames::output)] = new_output;
}

template class LerpNode<JitProvider>;
