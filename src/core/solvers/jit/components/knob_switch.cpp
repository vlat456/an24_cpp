#include "knob_switch.h"
#include "port_registry.h"
#include <cmath>
#include <algorithm>

template <typename Provider>
void KnobSwitch<Provider>::pre_load() {
    // Clamp initial position
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
    float control = st.values[provider.get(PortNames::control)];
    int requested = static_cast<int>(std::round(control));
    requested = std::clamp(requested, 0, positions - 1);
    selected = requested;

    // Write current position to output
    st.values[provider.get(PortNames::position)] = static_cast<float>(selected);
}

template class KnobSwitch<JitProvider>;
