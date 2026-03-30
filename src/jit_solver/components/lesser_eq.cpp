#include "lesser_eq.h"
#include "port_registry.h"

template <typename Provider>
void LesserEq<Provider>::execute(SimulationState& st, float /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = (A <= B) ? 1.0f : 0.0f;
}

template <typename Provider>
void LesserEq<Provider>::commit(SimulationState& st) {
    (void)st;
}

template class LesserEq<JitProvider>;
