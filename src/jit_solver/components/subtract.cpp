#include "subtract.h"
#include "port_registry.h"

template <typename Provider>
void Subtract<Provider>::execute(SimulationState& st, float /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = A - B;
}

template <typename Provider>
void Subtract<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class Subtract<JitProvider>;
