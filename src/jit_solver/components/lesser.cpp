#include "lesser.h"
#include "port_registry.h"

template <typename Provider>
void Lesser<Provider>::execute(SimulationState& st, float /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = (A < B) ? 1.0f : 0.0f;
}

template <typename Provider>
void Lesser<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class Lesser<JitProvider>;
