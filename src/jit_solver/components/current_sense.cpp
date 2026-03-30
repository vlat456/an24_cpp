#include "current_sense.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void CurrentSense<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Read solved branch current from electrical runtime state.
    // If no valid handle or no runtime state, output 0.0f.
    float i_out = 0.0f;
    if (st.electrical_rt != nullptr) {
        i_out = get_branch_current(*st.electrical_rt, electrical_handle);
    }
    st.values[provider.get(PortNames::i_out)] = i_out;
}

template <typename Provider>
void CurrentSense<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class CurrentSense<JitProvider>;
