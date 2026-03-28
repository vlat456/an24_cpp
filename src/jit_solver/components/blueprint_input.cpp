#include "blueprint_input.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void BlueprintInput<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // No-op - pass-through component (like Bus)
    // Union-find will collapse port to connected signal
    (void)st;
}

template <typename Provider>
void BlueprintInput<Provider>::execute(SimulationState& st, float dt) {
    solve_electrical(st, dt);
}

template class BlueprintInput<JitProvider>;
