#include "sample_hold.h"
#include "port_registry.h"

template <typename Provider>
void SampleHold<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t trig_idx = provider.get(PortNames::trigger);
    uint32_t out_idx = provider.get(PortNames::out);

    float val_in = st.values[in_idx];
    float trig_in = st.values[trig_idx];

    // Rising edge detector
    bool is_rising = (trig_in > 0.5f && last_trig <= 0.5f);
    last_trig = trig_in;

    // If rising edge, update stored value, otherwise keep old value
    stored_value = is_rising ? val_in : stored_value;

    st.values[out_idx] = stored_value;
}

template <typename Provider>
void SampleHold<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class SampleHold<JitProvider>;
