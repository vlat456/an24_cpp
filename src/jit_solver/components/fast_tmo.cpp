#include "fast_tmo.h"
#include "port_registry.h"
#include <cmath>

template <typename Provider>
void FastTMO<Provider>::pre_load() {
    inv_tau = 1.0f / std::max(tau, 0.0001f);
}

template <typename Provider>
void FastTMO<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.values[in_idx];

    // 1. Branchless Cold Start
    current_value += (input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    // 2. Branchless TMO Logic
    float diff = input - current_value;
    float factor = std::min(dt * inv_tau, 1.0f);
    // f32.select equivalent
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    current_value += diff * factor * dz_mask;
    st.values[out_idx] = current_value;
}

template <typename Provider>
void FastTMO<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class FastTMO<JitProvider>;
