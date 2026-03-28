#include "lesser_eq.h"
#include "port_registry.h"

template <typename Provider>
void LesserEq<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = (A <= B) ? 1.0f : 0.0f;
}

template <typename Provider>
void LesserEq<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class LesserEq<JitProvider>;
