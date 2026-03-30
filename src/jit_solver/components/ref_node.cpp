#include "ref_node.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void RefNode<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Push model: set v pin to configured value every solve
    st.values[provider.get(PortNames::v)] = value;
}

template <typename Provider>
void RefNode<Provider>::commit(SimulationState& st) {
    (void)st;
}

template class RefNode<JitProvider>;
