#include "slew_rate.h"
#include "port_registry.h"
#include <cmath>

template <typename Provider>
void SlewRate<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.values[in_idx];

    // 1. Instant initialization on first frame (branchless)
    current_value += (input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    // 2. Compute desired change
    float diff = input - current_value;

    // 3. Compute limit per step for current dt
    float max_step = max_rate * dt;

    // 4. Clamp differential (WASM friendly clamp)
    float limited_diff = std::max(-max_step, std::min(max_step, diff));

    // 5. Apply deadzone mask to avoid "dithering" around target
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    current_value += limited_diff * dz_mask;
    st.values[out_idx] = current_value;
}

template <typename Provider>
void SlewRate<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class SlewRate<JitProvider>;
