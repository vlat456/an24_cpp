#include "generator.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void Generator<Provider>::pre_load() {
    // Match Battery's safety pattern: floor resistance instead of zeroing out
    float safe_r = std::max(internal_r, 1e-6f);
    inv_internal_r = 1.0f / safe_r;
}

template <typename Provider>
void Generator<Provider>::execute(SimulationState& /*st*/, double /*dt*/) {
    // No-op: Generator is solver-owned. Electrical propagation runs via the
    // conductance matrix in the electrical subsolver, not via push scheduler.
}

template <typename Provider>
void Generator<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Generator<JitProvider>;
