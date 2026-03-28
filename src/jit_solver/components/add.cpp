#include "add.h"
#include "port_registry.h"

template <typename Provider>
void Add<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    float B = st.values[provider.get(PortNames::B)];
    st.values[provider.get(PortNames::o)] = A + B;
}

template <typename Provider>
void Add<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class Add<JitProvider>;
