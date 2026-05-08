#include "greater.h"
#include "core/solvers/common/port_names.h"

template <typename Provider>
void Greater<Provider>::execute(SimulationState& st, double /*dt*/) {
    float const A = st.values[provider.get(PortNames::A)];
    float const B = st.values[provider.get(PortNames::B)];
    // Branchless: результат сравнения приводится к float (1.0 или 0.0)
    st.values[provider.get(PortNames::o)] = (A > B) ? 1.0f : 0.0f;
}

template <typename Provider>
void Greater<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Greater<JitProvider>;
