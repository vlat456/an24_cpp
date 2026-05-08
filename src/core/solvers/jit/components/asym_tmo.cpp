#include "asym_tmo.h"
#include "core/solvers/common/port_names.h"
#include <cmath>

template <typename Provider>
void AsymTMO<Provider>::pre_load() {
    inv_tau_up = 1.0f / std::max(tau_up, 0.0001f);
    inv_tau_down = 1.0f / std::max(tau_down, 0.0001f);
}

template <typename Provider>
void AsymTMO<Provider>::execute(SimulationState& st, double dt) {
    uint32_t const in_idx = provider.get(PortNames::in);
    uint32_t const out_idx = provider.get(PortNames::out);
    float const input = st.values[in_idx];

    // === Two-Phase State Semantics ===

    // Phase 1 (execute): Read from COMMITTED state
    // Cold start initialization
    float const committed_value = current_value + (input - current_value) * first_frame_mask;
    float const committed_mask = 0.0f; // first_frame_mask consumed

    // Branchless Asymmetric Logic
    float const diff = input - committed_value;
    // WASM f32.select for tau selection
    float const active_inv_tau = (diff > 0.0f) ? inv_tau_up : inv_tau_down;

    float const factor = std::min(static_cast<float>(dt) * active_inv_tau, 1.0f);
    float const dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    // Compute next value
    float const new_value = committed_value + diff * factor * dz_mask;

    // Stage next state
    next_current_value = new_value;
    next_first_frame_mask = committed_mask;

    // Output from COMMITTED current_value - one-frame delay
    st.values[out_idx] = committed_value;
}

template <typename Provider>
void AsymTMO<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    // Commit staged next state
    current_value = next_current_value;
    first_frame_mask = next_first_frame_mask;
}

template class AsymTMO<JitProvider>;
