#include "lerp_node.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void LerpNode<Provider>::execute(SimulationState& st, float dt) {
    float v_input = st.values[provider.get(PortNames::input)];
    (void)dt;

    // Read from COMMITTED state
    float committed_value = current_value + (v_input - current_value) * first_frame_mask;
    float committed_mask = 0.0f; // first_frame_mask consumed

    float diff = v_input - committed_value;
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    float new_output = committed_value + factor * diff * dz_mask;

    // Stage next state (not directly committed)
    next_current_value = new_output;
    next_first_frame_mask = committed_mask;

    // Output from COMMITTED state (one-frame delay)
    st.values[provider.get(PortNames::output)] = committed_value;
}

template <typename Provider>
void LerpNode<Provider>::commit(SimulationState& /*st*/) {
    // Commit staged next state
    current_value = next_current_value;
    first_frame_mask = next_first_frame_mask;
}

template class LerpNode<JitProvider>;
