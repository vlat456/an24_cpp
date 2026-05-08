#include "accumulator.h"
#include "core/solvers/common/port_names.h"

template <typename Provider>
void Accumulator<Provider>::execute(SimulationState& st, double dt) {
    uint32_t const in_idx = provider.get(PortNames::in);
    uint32_t const out_idx = provider.get(PortNames::out);

    float const val_in = st.values[in_idx];

    // === Two-Phase State Semantics ===

    // Phase 1 (execute): Read from COMMITTED state
    // Cold Start: snap to initial_val on first frame
    float const committed = state + (initial_val - state) * first_frame_mask;
    float const committed_mask = 0.0f; // first_frame_mask consumed

    // Accumulate: state += in * dt
    float const new_state = committed + val_in * dt;

    // Stage next state
    next_state = new_state;
    next_first_frame_mask = committed_mask;

    // Output from COMMITTED state (one-frame delay)
    st.values[out_idx] = committed;
}

template <typename Provider>
void Accumulator<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    // Commit staged next state
    state = next_state;
    first_frame_mask = next_first_frame_mask;
}

template class Accumulator<JitProvider>;
