#include "bus.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void Bus<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Bus is just a wire - no component behavior in push model
    // Union-find will collapse port to connected signal
    (void)st;
}

template <typename Provider>
void Bus<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Bus<JitProvider>;
