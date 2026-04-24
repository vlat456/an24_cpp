#include "sample_hold.h"
#include "core/solvers/common/port_registry.h"

template <typename Provider>
void SampleHold<Provider>::execute(SimulationState& st, double /*dt*/) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t trig_idx = provider.get(PortNames::trigger);
    uint32_t out_idx = provider.get(PortNames::out);

    float val_in = st.values[in_idx];
    float trig_in = st.values[trig_idx];

    // === Two-Phase State Semantics ===

    // Phase 1 (execute): Rising edge detector from COMMITTED last_trig
    bool is_rising = (trig_in > 0.5f && last_trig <= 0.5f);

    // If rising edge, update stored value, otherwise keep committed value
    float new_stored_value = is_rising ? val_in : stored_value;

    // Stage next state
    next_stored_value = new_stored_value;
    next_last_trig = trig_in;

    // Output from COMMITTED stored_value - one-frame delay for sample
    st.values[out_idx] = stored_value;
}

template <typename Provider>
void SampleHold<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    // Commit staged next state
    stored_value = next_stored_value;
    last_trig = next_last_trig;
}

template class SampleHold<JitProvider>;
