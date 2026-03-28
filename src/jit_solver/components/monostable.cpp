#include "monostable.h"
#include "port_registry.h"
#include <algorithm>

template <typename Provider>
void Monostable<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);

    // Convert input to 0.0 or 1.0
    float raw_in = (st.values[in_idx] > 0.5f) ? 1.0f : 0.0f;

    // Rising edge detector
    bool trigger = (raw_in > 0.5f && last_in <= 0.5f);
    last_in = raw_in;

    // If triggered, reset timer to duration, otherwise tick down to 0
    timer = trigger ? duration : std::max(0.0f, timer - dt);

    // Output is active while timer > 0
    st.values[out_idx] = (timer > 0.0f) ? 1.0f : 0.0f;
}

template <typename Provider>
void Monostable<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class Monostable<JitProvider>;
