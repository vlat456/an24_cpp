#include "gyroscope.h"
#include "core/solvers/common/port_registry.h"
#include "../state.h"

template <typename Provider>
void Gyroscope<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Push model: single-port component (connects to ground)
    // Just reads the input value - no output to write in push model
    (void)st.values[provider.get(PortNames::input)];
}

template <typename Provider>
void Gyroscope<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Gyroscope<JitProvider>;
