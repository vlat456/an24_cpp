#include "splitter.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void Splitter<Provider>::execute(SimulationState& st, float /*dt*/) {
    float val = st.values[provider.get(PortNames::i)];
    st.values[provider.get(PortNames::o1)] = val;
    st.values[provider.get(PortNames::o2)] = val;
}

template <typename Provider>
void Splitter<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class Splitter<JitProvider>;
