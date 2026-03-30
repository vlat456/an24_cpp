#include "splitter.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void Splitter<Provider>::execute(SimulationState& st, float /*dt*/) {
    // No-op: splitter is a pass-through for signal routing
    (void)st;
}

template <typename Provider>
void Splitter<Provider>::commit(SimulationState& st) {
    (void)st;
}

template class Splitter<JitProvider>;
