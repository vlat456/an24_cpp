#include "subtract.h"
#include "core/solvers/common/port_names.h"

template <typename Provider>
void Subtract<Provider>::execute(SimulationState& st, double /*dt*/) {
    float const A = st.values[provider.get(PortNames::A)];
    float const B = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = A - B;
}

template <typename Provider>
void Subtract<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Subtract<JitProvider>;
