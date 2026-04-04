#include "voltmeter.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void Voltmeter<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Voltmeter is observation-only — reads v_in, does not modify circuit.
    // Port registry defines only v_in for Voltmeter; v_out does not exist.
    (void)st;
}

template <typename Provider>
void Voltmeter<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Voltmeter<JitProvider>;
