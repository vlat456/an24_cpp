#include "add.h"
#include "port_registry.h"

template <typename Provider>
void Add<Provider>::execute(SimulationState& st, double /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = A + B;
}

template <typename Provider>
void Add<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Add<JitProvider>;
