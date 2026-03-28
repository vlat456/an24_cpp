#include "xor_gate.h"
#include "port_registry.h"

template <typename Provider>
void XOR<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    bool a = (A > 0.5f);
    bool b = (B > 0.5f);
    bool result = a != b;
    st.values[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void XOR<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class XOR<JitProvider>;
