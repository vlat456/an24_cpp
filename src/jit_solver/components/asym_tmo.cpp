#include "asym_tmo.h"
#include "port_registry.h"
#include <cmath>

template <typename Provider>
void AsymTMO<Provider>::pre_load() {
    inv_tau_up = 1.0f / std::max(tau_up, 0.0001f);
    inv_tau_down = 1.0f / std::max(tau_down, 0.0001f);
}

template <typename Provider>
void AsymTMO<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.values[in_idx];

    // 1. Branchless Cold Start
    current_value += (input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    // 2. Branchless Asymmetric Logic
    float diff = input - current_value;
    // WASM f32.select for tau selection
    float active_inv_tau = (diff > 0.0f) ? inv_tau_up : inv_tau_down;

    float factor = std::min(dt * active_inv_tau, 1.0f);
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    current_value += diff * factor * dz_mask;
    st.values[out_idx] = current_value;
}

template <typename Provider>
void AsymTMO<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class AsymTMO<JitProvider>;
