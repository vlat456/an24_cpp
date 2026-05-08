#include "comparator.h"
#include "core/solvers/common/port_names.h"

template <typename Provider>
void Comparator<Provider>::pre_load() {
    // Parameters are set by factory from JSON params
}

template <typename Provider>
void Comparator<Provider>::execute(SimulationState& st, double /*dt*/) {
    float const Va = st.values[provider.get(PortNames::Va)];
    float const Vb = st.values[provider.get(PortNames::Vb)];

    float const diff = Va - Vb;

    bool const set = (diff >= Von);
    bool const keep = (diff > Voff);
    output_state = set || (output_state && keep);

    st.values[provider.get(PortNames::o)] = output_state ? 1.0f : 0.0f;
}

template <typename Provider>
void Comparator<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Comparator<JitProvider>;
