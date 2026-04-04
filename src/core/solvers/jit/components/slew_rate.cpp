#include "slew_rate.h"
#include "port_registry.h"
#include <cmath>

template <typename Provider>
void SlewRate<Provider>::execute(SimulationState& st, double dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.values[in_idx];

    // === Two-Phase State Semantics ===

    // Phase 1 (execute): Read from COMMITTED state
    // Cold start initialization
    float committed_value = current_value + (input - current_value) * first_frame_mask;
    float committed_mask = 0.0f; // first_frame_mask consumed

    // Compute desired change from committed state
    float diff = input - committed_value;

    // Compute limit per step for current dt
    float max_step = max_rate * dt;

    // Clamp differential (WASM friendly clamp)
    float limited_diff = std::max(-max_step, std::min(max_step, diff));

    // Apply deadzone mask to avoid "dithering" around target
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    // Compute next value
    float new_value = committed_value + limited_diff * dz_mask;

    // Stage next state
    next_current_value = new_value;
    next_first_frame_mask = committed_mask;

    // Output from COMMITTED current_value - one-frame delay
    st.values[out_idx] = committed_value;
}

template <typename Provider>
void SlewRate<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    // Commit staged next state
    current_value = next_current_value;
    first_frame_mask = next_first_frame_mask;
}

template class SlewRate<JitProvider>;
