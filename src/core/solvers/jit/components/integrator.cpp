#include "integrator.h"
#include "core/solvers/common/port_names.h"

template <typename Provider>
void Integrator<Provider>::execute(SimulationState& st, double dt) {
    uint32_t const in_idx = provider.get(PortNames::in);
    uint32_t const reset_idx = provider.get(PortNames::reset);
    uint32_t const out_idx = provider.get(PortNames::out);

    float const val_in = st.values[in_idx];
    float const reset_in = st.values[reset_idx];
    float const g = st.values[provider.get(PortNames::gain)];

    // === Two-Phase State Semantics ===

    // Phase 1 (execute): Read from COMMITTED state
    // Cold Start
    float const committed_acc = accumulator + (initial_val - accumulator) * first_frame_mask;
    float const committed_mask = 0.0f; // first_frame_mask consumed

    // Integration: accumulate with gain scaling
    float const integrated = committed_acc + val_in * g * dt;

    // Reset: if reset signal > 0.5, zero out (branchless)
    float const new_accumulator = (reset_in > 0.5f) ? 0.0f : integrated;

    // Stage next state
    next_accumulator = new_accumulator;
    next_first_frame_mask = committed_mask;

    // Output from COMMITTED accumulator - one-frame delay
    st.values[out_idx] = committed_acc;
}

template <typename Provider>
void Integrator<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    // Commit staged next state
    accumulator = next_accumulator;
    first_frame_mask = next_first_frame_mask;
}

template class Integrator<JitProvider>;
