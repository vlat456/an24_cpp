#include "time_delay.h"
#include "port_registry.h"

template <typename Provider>
void TimeDelay<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);

    // Convert input to 0.0 or 1.0
    float raw_in = (st.values[in_idx] > 0.5f) ? 1.0f : 0.0f;

    // 1. Cold start (branchless)
    current_out += (raw_in - current_out) * first_frame_mask;
    last_in += (raw_in - last_in) * first_frame_mask;  // Sync on cold start only
    first_frame_mask = 0.0f;

    // 2. Reset logic: if input changed from last frame, zero the timer
    // WASM f32.select: keep accumulator if raw_in == last_in, else 0
    accumulator = (raw_in == last_in) ? (accumulator + dt) : 0.0f;
    last_in = raw_in;

    // 3. Select time threshold (delay_on if targeting 1, delay_off if targeting 0)
    float target_delay = (raw_in > 0.5f) ? delay_on : delay_off;

    // 4. Check timer expiration and state difference
    bool timer_expired = (accumulator >= target_delay);
    bool state_differs = (raw_in != current_out);

    // Update output only if timer expired
    current_out = (timer_expired && state_differs) ? raw_in : current_out;

    st.values[out_idx] = current_out;
}

template <typename Provider>
void TimeDelay<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class TimeDelay<JitProvider>;
