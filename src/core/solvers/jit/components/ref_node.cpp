#include "ref_node.h"
#include "core/solvers/common/port_names.h"
#include "../state.h"

template <typename Provider>
void RefNode<Provider>::execute(SimulationState& st, double /*dt*/) {
    // RefNode IS correctly scheduled as a source in the push scheduler.
    // It writes its fixed reference value to the signal array each frame
    // so downstream logical consumers see the correct reference voltage.
    // This is correct behavior: RefNode defines the reference point (0V),
    // it does not participate in the electrical solver's conductance matrix.
    st.values[provider.get(PortNames::v)] = value;
}

template <typename Provider>
void RefNode<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class RefNode<JitProvider>;
