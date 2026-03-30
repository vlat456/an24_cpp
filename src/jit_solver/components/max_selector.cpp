#include "max_selector.h"
#include "port_registry.h"
#include <algorithm>

template <typename Provider>
void MaxSelector<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Reuse existing logical port naming convention (A/B/o) for compatibility
    // with current generated port registry.
    const float a = st.values[provider.get(PortNames::A)];
    const float b = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = std::max(a, b);
}

template <typename Provider>
void MaxSelector<Provider>::commit(SimulationState& st) {
    (void)st;
}

template class MaxSelector<JitProvider>;
