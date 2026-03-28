#include "comparator.h"
#include "port_registry.h"

template <typename Provider>
void Comparator<Provider>::pre_load() {
    // Parameters are set by factory from JSON params
}

template <typename Provider>
void Comparator<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float Va = st.values[provider.get(PortNames::Va)];
    float Vb = st.values[provider.get(PortNames::Vb)];

    float diff = Va - Vb;

    bool set = (diff >= Von);
    bool keep = (diff > Voff);
    output_state = set || (output_state && keep);

    st.values[provider.get(PortNames::o)] = output_state ? 1.0f : 0.0f;
}

template <typename Provider>
void Comparator<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class Comparator<JitProvider>;
