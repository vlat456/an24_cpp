#include "time_delay.h"
#include "port_registry.h"

template <typename Provider>
void TimeDelay<Provider>::execute(SimulationState& st, double dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);

    // Convert input to 0.0 or 1.0
    float raw_in = (st.values[in_idx] > 0.5f) ? 1.0f : 0.0f;

    // === Two-Phase State Semantics ===

    // Phase 1 (execute): Read committed state, compute output, stage next state

    // 1a. Cold start initialization from committed state
    float init_current_out = current_out + (raw_in - current_out) * first_frame_mask;
    float init_last_in = last_in + (raw_in - last_in) * first_frame_mask;
    float init_first_frame_mask = 0.0f;

    // 1b. Reset logic: if input changed from committed last_in, zero the timer
    float new_accumulator = (raw_in == init_last_in) ? (accumulator + dt) : 0.0f;
    float new_last_in = raw_in;

    // 1c. Select time threshold (delay_on if targeting 1, delay_off if targeting 0)
    float target_delay = (raw_in > 0.5f) ? delay_on : delay_off;

    // 1d. Check timer expiration and state difference
    bool timer_expired = (new_accumulator >= target_delay);
    bool state_differs = (raw_in != init_current_out);

    // 1e. Compute next output only if timer expired
    float new_current_out = (timer_expired && state_differs) ? raw_in : init_current_out;

    // Stage next state
    next_current_out = new_current_out;
    next_last_in = new_last_in;
    next_accumulator = new_accumulator;
    next_first_frame_mask = init_first_frame_mask;

    // Output from committed (cold-start-adjusted) current_out - one-frame delay
    st.values[out_idx] = init_current_out;
}

template <typename Provider>
void TimeDelay<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    // Commit staged next state
    current_out = next_current_out;
    last_in = next_last_in;
    accumulator = next_accumulator;
    first_frame_mask = next_first_frame_mask;
}

template class TimeDelay<JitProvider>;
