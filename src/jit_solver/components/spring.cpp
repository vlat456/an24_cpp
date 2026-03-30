#include "spring.h"
#include "port_registry.h"
#include <cmath>

template <typename Provider>
void Spring<Provider>::execute(SimulationState& st, float dt) {
    float pA = st.values[provider.get(PortNames::pos_a)];
    float pB = st.values[provider.get(PortNames::pos_b)];

    // 1. Calculate current deformation
    float delta_x = (pA - pB) - rest_length;

    // 2. Branchless cold start: initialize prev_delta_x on first frame
    prev_delta_x += (delta_x - prev_delta_x) * first_frame_mask;
    first_frame_mask = 0.0f;

    // 3. Spring force (Hooke's Law): F_spring = k * |delta_x|
    float spring_force = delta_x * k;

    // 4. Viscous damping force: F_damp = c * velocity
    //    velocity ≈ (delta_x - prev_delta_x) / dt (finite difference)
    //    One division per mechanical step (20 Hz) — acceptable
    float inv_dt = 1.0f / std::max(dt, 1e-6f);
    float velocity = (delta_x - prev_delta_x) * inv_dt;
    float damping_force = c * velocity;

    // 5. Total force = spring + damping (both resist motion)
    float total_force = spring_force + damping_force;

    // 6. If spring works only in compression (like in RUG-82 governor),
    //    cut off stretching forces (branchless select)
    //    co_f: 1.0 means "compression only mode", 0.0 means "both directions"
    //    When co_f == 1.0: mask = (delta_x < 0) ? 1 : 0
    //    When co_f == 0.0: mask = 1.0 (always active)
    float co_f = static_cast<float>(compression_only); // branchless bool→float (0.0 or 1.0)
    float comp_mask = (delta_x < 0.0f) ? 1.0f : 0.0f;
    float compression_mask = comp_mask * co_f + (1.0f - co_f);

    // 7. Result: std::abs ensures force magnitude is always non-negative
    st.values[provider.get(PortNames::force_out)] = std::abs(total_force) * compression_mask;

    // 8. Store for next frame
    prev_delta_x = delta_x;
}

template <typename Provider>
void Spring<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template <typename Provider>
void Spring<Provider>::pre_load() {
    // No precomputation needed.
}

template class Spring<JitProvider>;
