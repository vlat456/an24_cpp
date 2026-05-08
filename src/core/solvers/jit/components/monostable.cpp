#include "monostable.h"
#include "core/solvers/common/port_names.h"
#include <algorithm>

template <typename Provider>
void Monostable<Provider>::execute(SimulationState& st, double dt) {
    uint32_t const in_idx = provider.get(PortNames::in);
    uint32_t const out_idx = provider.get(PortNames::out);

    // Convert input to 0.0 or 1.0
    float const raw_in = (st.values[in_idx] > 0.5f) ? 1.0f : 0.0f;

    // === Two-Phase State Semantics ===

    // Phase 1 (execute): Rising edge detector from COMMITTED last_in
    bool const trigger = (raw_in > 0.5f && last_in <= 0.5f);

    // Compute next_timer: if triggered, reset to duration; otherwise tick down
    float const new_timer = trigger ? duration : std::max(0.0f, timer - static_cast<float>(dt));

    // Stage next state
    next_timer = new_timer;
    next_last_in = raw_in;

    // Output from COMMITTED timer (active if >0) - one-frame delay for trigger response
    st.values[out_idx] = (timer > 0.0f) ? 1.0f : 0.0f;
}

template <typename Provider>
void Monostable<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    // Commit staged next state
    timer = next_timer;
    last_in = next_last_in;
}

template class Monostable<JitProvider>;
