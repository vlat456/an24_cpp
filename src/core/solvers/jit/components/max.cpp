#include "max.h"
#include "core/solvers/common/port_names.h"
#include <algorithm>

template <typename Provider>
void Max<Provider>::execute(SimulationState& st, double /*dt*/) {
    float const A = st.values[provider.get(PortNames::A)];
    float const B = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = std::max(A, B);
}

template <typename Provider>
void Max<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Max<JitProvider>;
