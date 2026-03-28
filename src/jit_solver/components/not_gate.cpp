#include "not_gate.h"
#include "port_registry.h"

template <typename Provider>
void NOT<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.values[provider.get(PortNames::A)];
    bool a = (A > 0.5f);
    bool result = !a;
    st.values[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void NOT<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class NOT<JitProvider>;
