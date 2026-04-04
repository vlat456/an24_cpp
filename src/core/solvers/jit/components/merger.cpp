#include "merger.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void Merger<Provider>::execute(SimulationState& st, double /*dt*/) {
    // No-op: merger is a pass-through for signal routing
    (void)st;
}

template <typename Provider>
void Merger<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Merger<JitProvider>;
