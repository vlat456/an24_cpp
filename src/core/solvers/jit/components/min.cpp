#include "min.h"
#include "core/solvers/common/port_names.h"
#include <algorithm>

template <typename Provider>
void Min<Provider>::execute(SimulationState& st, double /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = std::min(A, B);
}

template <typename Provider>
void Min<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Min<JitProvider>;
