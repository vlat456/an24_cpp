#include "electrical_source.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void ElectricalSource<Provider>::execute(SimulationState& /*st*/, double /*dt*/) {
    // No-op: ElectricalSource is a solver-owned primitive.
    // Electrical propagation runs via the conductance matrix in the
    // electrical subsolver, not via push scheduler.
}

template <typename Provider>
void ElectricalSource<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    // No state to update.
}

template class ElectricalSource<JitProvider>;
