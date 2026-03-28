#include "divide.h"
#include "port_registry.h"
#include <cmath>

template <typename Provider>
void Divide<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    // Avoid division by zero - if B is effectively zero, output zero
    float result = (std::abs(B) > 1e-6f) ? (A / B) : 0.0f;
    st.values[provider.get(PortNames::o)] = result;
}

template <typename Provider>
void Divide<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class Divide<JitProvider>;
