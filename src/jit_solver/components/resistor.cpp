#include "resistor.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void Resistor<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Push model: Resistor as pass-through element
    // In a simple push model, we propagate v_in to v_out
    // The actual voltage drop would require solving, but for push-friendly
    // behavior we do simple pass-through (could be enhanced later)
    float v_in = st.values[provider.get(PortNames::v_in)];
    st.values[provider.get(PortNames::v_out)] = v_in;
}

template <typename Provider>
void Resistor<Provider>::commit(SimulationState& st) {
    (void)st;
}

template class Resistor<JitProvider>;
