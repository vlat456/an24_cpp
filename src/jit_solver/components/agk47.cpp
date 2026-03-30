#include "agk47.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void AGK47<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Push model: single-port component (connects to ground)
    // Just reads the input value - no output to write in push model
    (void)st.values[provider.get(PortNames::input)];
}

template <typename Provider>
void AGK47<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class AGK47<JitProvider>;
