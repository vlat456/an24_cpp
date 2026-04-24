#include "electrical_conductance.h"
#include "core/solvers/common/port_registry.h"
#include "../state.h"

template <typename Provider>
void ElectricalConductance<Provider>::execute(SimulationState& /*st*/, double /*dt*/) {
    // No-op: ElectricalConductance is a solver-owned primitive.
    // Electrical propagation runs via the conductance matrix in the
    // electrical subsolver, not via push scheduler.
}

template <typename Provider>
void ElectricalConductance<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    // No state to update.
}

template class ElectricalConductance<JitProvider>;
