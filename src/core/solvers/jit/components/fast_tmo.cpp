#include "fast_tmo.h"
#include "core/solvers/common/port_names.h"
#include <cmath>

template <typename Provider>
void FastTMO<Provider>::pre_load() {
    inv_tau = 1.0f / std::max(tau, 0.0001f);
}

template <typename Provider>
void FastTMO<Provider>::execute(SimulationState& st, double dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.values[in_idx];

    // === Two-Phase State Semantics ===

    // Phase 1 (execute): Read from COMMITTED state
    // Cold start initialization
    float committed_value = current_value + (input - current_value) * first_frame_mask;
    float committed_mask = 0.0f; // first_frame_mask consumed

    // Branchless TMO Logic
    float diff = input - committed_value;
    float factor = std::min(static_cast<float>(dt) * inv_tau, 1.0f);
    // f32.select equivalent
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    // Compute next value
    float new_value = committed_value + diff * factor * dz_mask;

    // Stage next state
    next_current_value = new_value;
    next_first_frame_mask = committed_mask;

    // Output from COMMITTED current_value - one-frame delay
    st.values[out_idx] = committed_value;
}

template <typename Provider>
void FastTMO<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    // Commit staged next state
    current_value = next_current_value;
    first_frame_mask = next_first_frame_mask;
}

template class FastTMO<JitProvider>;
