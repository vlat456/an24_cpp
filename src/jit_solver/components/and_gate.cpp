#include "and_gate.h"
#include "port_registry.h"

template <typename Provider>
void AND<Provider>::execute(SimulationState& st, float /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    // Treat > 0.5V as TRUE, else FALSE
    bool a = (A > 0.5f);
    bool b = (B > 0.5f);
    bool result = a && b;
    st.values[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void AND<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class AND<JitProvider>;
