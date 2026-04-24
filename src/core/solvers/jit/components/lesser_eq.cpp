#include "lesser_eq.h"
#include "core/solvers/common/port_registry.h"

template <typename Provider>
void LesserEq<Provider>::execute(SimulationState& st, double /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = (A <= B) ? 1.0f : 0.0f;
}

template <typename Provider>
void LesserEq<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class LesserEq<JitProvider>;
