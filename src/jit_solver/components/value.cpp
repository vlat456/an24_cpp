#include "value.h"
#include "port_registry.h"

template <typename Provider>
void Value<Provider>::execute(SimulationState& st, float /*dt*/) {
    st.values[provider.get(PortNames::o)] = value;
}

template <typename Provider>
void Value<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class Value<JitProvider>;
