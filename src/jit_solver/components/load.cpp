#include "load.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void Load<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Push model: Load is a consume-only component in push model
    // No output to set - it just reads input and "consumes" it
    // In a full implementation, this could track power consumption
    // For now, this is a no-op on the electrical output side
    (void)st;
}

template <typename Provider>
void Load<Provider>::execute(SimulationState& st, float dt) {
    solve_electrical(st, dt);
}

template class Load<JitProvider>;
