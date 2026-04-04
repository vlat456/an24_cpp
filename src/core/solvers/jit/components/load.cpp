#include "load.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void Load<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Push model: Load is a consume-only component in push model
    // No output to set - it just reads input and "consumes" it
    // In a full implementation, this could track power consumption
    // For now, this is a no-op on the electrical output side
    (void)st;
}

template <typename Provider>
void Load<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Load<JitProvider>;
