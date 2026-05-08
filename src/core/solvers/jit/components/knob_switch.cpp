#include "knob_switch.h"
#include "core/solvers/common/port_names.h"
#include <cmath>
#include <algorithm>

template <typename Provider>
void KnobSwitch<Provider>::pre_load() {
    // Clamp positions count and initial position
    positions = std::clamp(positions, 2, MAX_POSITIONS);
    selected = std::clamp(selected, 0, positions - 1);
}

template <typename Provider>
void KnobSwitch<Provider>::execute(SimulationState& /*st*/, double /*dt*/) {
    // Electrical behavior is fully solver-owned via dynamic conductance branches.
    // No per-frame computation needed in execute().
}

template <typename Provider>
void KnobSwitch<Provider>::commit(SimulationState& st, double /*dt*/) {
    // Read control input — interpreted as 0-based position index
    float const control = st.values[provider.get(PortNames::control)];

    // Edge detection: on first commit (last_control == -1.0 sentinel),
    // preserve factory-set initial_position when control is at default (0.0).
    // Bootstrap commit runs before Value components initialize signals,
    // so control is still 0.0 — we must not overwrite initial_position.
    if (last_control < -0.5f && std::abs(control) < 0.1f) {
        last_control = control;
        st.values[provider.get(PortNames::position)] = static_cast<float>(selected);
        return;
    }

    int requested = static_cast<int>(std::round(control));
    requested = std::clamp(requested, 0, positions - 1);
    selected = requested;
    last_control = control;

    // Write current position to output
    st.values[provider.get(PortNames::position)] = static_cast<float>(selected);
}

template class KnobSwitch<JitProvider>;
