#include "value.h"
#include "core/solvers/common/port_names.h"

template <typename Provider>
void Value<Provider>::execute(SimulationState& st, double /*dt*/) {
    st.values[provider.get(PortNames::o)] = value;
}

template <typename Provider>
void Value<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Value<JitProvider>;
