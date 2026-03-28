#include "merger.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void Merger<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Merger<Provider>::execute(SimulationState& st, float dt) {
    // No-op: merger is a pass-through for signal routing
    (void)st;
    (void)dt;
}

template <typename Provider>
void Merger<Provider>::solve_mechanical(SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Merger<Provider>::solve_hydraulic(SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Merger<Provider>::solve_thermal(SimulationState& st, float /*dt*/) {}

template class Merger<JitProvider>;
