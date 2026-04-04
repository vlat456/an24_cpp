#include "blueprint_input.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void BlueprintInput<Provider>::execute(SimulationState& st, double /*dt*/) {
    // No-op - pass-through component (like Bus)
    // Union-find will collapse port to connected signal
    (void)st;
}

template <typename Provider>
void BlueprintInput<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class BlueprintInput<JitProvider>;
