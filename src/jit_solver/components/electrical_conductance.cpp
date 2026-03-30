#include "electrical_conductance.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void ElectricalConductance<Provider>::execute(SimulationState& /*st*/, float /*dt*/) {
    // No-op: ElectricalConductance is a solver-owned primitive.
    // Electrical propagation runs via the conductance matrix in the
    // electrical subsolver, not via push scheduler.
}

template <typename Provider>
void ElectricalConductance<Provider>::commit(SimulationState& /*st*/, float /*dt*/) {
    // No state to update.
}

template class ElectricalConductance<JitProvider>;
