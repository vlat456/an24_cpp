#include "inertia_node.h"
#include "port_registry.h"

template <typename Provider>
void InertiaNode<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Push model: pass-through for mechanical connection
    float v_input = st.values[provider.get(PortNames::input)];
    st.values[provider.get(PortNames::output)] = v_input;
}

template <typename Provider>
void InertiaNode<Provider>::commit(SimulationState& st) {
    (void)st;
}

template <typename Provider>
void InertiaNode<Provider>::pre_load() {
    inv_mass = 1.0f / std::max(mass, 1e-6f);
}

template class InertiaNode<JitProvider>;
