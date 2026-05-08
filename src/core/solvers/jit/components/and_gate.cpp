#include "and_gate.h"
#include "core/solvers/common/port_names.h"

template <typename Provider>
void AND<Provider>::execute(SimulationState& st, double /*dt*/) {
    float const A = st.values[provider.get(PortNames::A)];
    float const B = st.values[provider.get(PortNames::B)];
    // Treat > 0.5V as TRUE, else FALSE
    bool const a = (A > 0.5f);
    bool const b = (B > 0.5f);
    bool const result = a && b;
    st.values[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void AND<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class AND<JitProvider>;
