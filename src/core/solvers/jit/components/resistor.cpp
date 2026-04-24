#include "resistor.h"
#include "core/solvers/common/port_registry.h"
#include "../state.h"

template <typename Provider>
void Resistor<Provider>::execute(SimulationState& /*st*/, double /*dt*/) {
    // No-op: Resistor is solver-owned. Electrical propagation runs via the
    // conductance matrix in the electrical subsolver, not via push scheduler.
}

template <typename Provider>
void Resistor<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Resistor<JitProvider>;
