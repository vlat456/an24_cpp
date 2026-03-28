#include "max.h"
#include "port_registry.h"
#include <algorithm>

template <typename Provider>
void Max<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = std::max(A, B);
}

template <typename Provider>
void Max<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class Max<JitProvider>;
