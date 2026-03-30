#include "resistor.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void Resistor<Provider>::execute(SimulationState& /*st*/, float /*dt*/) {
    // No-op: Resistor is solver-owned. Electrical propagation runs via the
    // conductance matrix in the electrical subsolver, not via push scheduler.
}

template <typename Provider>
void Resistor<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class Resistor<JitProvider>;
