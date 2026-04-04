#include "clamp.h"
#include "port_registry.h"
#include <algorithm>

template <typename Provider>
void Clamp<Provider>::execute(SimulationState& st, double /*dt*/) {
    float input = st.values[provider.get(PortNames::in)];
    float lo = st.values[provider.get(PortNames::min)];
    float hi = st.values[provider.get(PortNames::max)];

    st.values[provider.get(PortNames::out)] = std::clamp(input, lo, hi);
}

template <typename Provider>
void Clamp<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Clamp<JitProvider>;
