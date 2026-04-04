#include "blueprint_output.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void BlueprintOutput<Provider>::execute(SimulationState& st, double /*dt*/) {
    // No-op - pass-through component (like Bus)
    // Union-find will collapse port to connected signal
    (void)st;
}

template <typename Provider>
void BlueprintOutput<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class BlueprintOutput<JitProvider>;
