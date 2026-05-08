#include "clamp.h"
#include "core/solvers/common/port_names.h"
#include <algorithm>

template <typename Provider>
void Clamp<Provider>::execute(SimulationState& st, double /*dt*/) {
    float const input = st.values[provider.get(PortNames::in)];
    float const lo = st.values[provider.get(PortNames::min)];
    float const hi = st.values[provider.get(PortNames::max)];

    st.values[provider.get(PortNames::out)] = std::clamp(input, lo, hi);
}

template <typename Provider>
void Clamp<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Clamp<JitProvider>;
