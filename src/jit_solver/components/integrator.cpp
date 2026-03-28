#include "integrator.h"
#include "port_registry.h"

template <typename Provider>
void Integrator<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t reset_idx = provider.get(PortNames::reset);
    uint32_t out_idx = provider.get(PortNames::out);

    float val_in = st.values[in_idx];
    float reset_in = st.values[reset_idx];

    // 1. Cold Start
    accumulator += (initial_val - accumulator) * first_frame_mask;
    first_frame_mask = 0.0f;

    // 2. Integration: accumulate with gain scaling
    accumulator += val_in * gain * dt;

    // 3. Reset: if reset signal > 0.5, zero out (branchless)
    accumulator = (reset_in > 0.5f) ? 0.0f : accumulator;

    st.values[out_idx] = accumulator;
}

template <typename Provider>
void Integrator<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class Integrator<JitProvider>;
