#include "asym_slew_rate.h"
#include "port_registry.h"
#include <cmath>

template <typename Provider>
void AsymSlewRate<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.values[in_idx];

    // 1. Branchless Cold Start
    current_value += (input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    float diff = input - current_value;

    // 2. Select active rate (WASM f32.select)
    // If rising - rate_up, if falling - rate_down
    float active_rate = (diff > 0.0f) ? rate_up : rate_down;

    // 3. Limit step for current frame
    float max_step = active_rate * dt;

    // 4. Branchless Clamp & Deadzone
    // Limit increment to [-max_step, max_step]
    float limited_diff = std::max(-max_step, std::min(max_step, diff));
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    current_value += limited_diff * dz_mask;
    st.values[out_idx] = current_value;
}

template <typename Provider>
void AsymSlewRate<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class AsymSlewRate<JitProvider>;
