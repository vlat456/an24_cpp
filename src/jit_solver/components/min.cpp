#include "min.h"
#include "port_registry.h"
#include <algorithm>

template <typename Provider>
void Min<Provider>::execute(SimulationState& st, float /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = std::min(A, B);
}

template <typename Provider>
void Min<Provider>::commit(SimulationState& st) {
    (void)st;
}

template class Min<JitProvider>;
