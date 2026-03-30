#include "battery.h"
#include "port_registry.h"
#include <cmath>

template <typename Provider>
void Battery<Provider>::pre_load() {
    float safe_r = std::max(internal_r, 1e-6f);
    inv_internal_r = 1.0f / safe_r;
}

template <typename Provider>
void Battery<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Push model: set v_out = v_in + v_nominal (voltage source behavior)
    // Read v_in, write v_out with offset
    float v_in = st.values[provider.get(PortNames::v_in)];
    st.values[provider.get(PortNames::v_out)] = v_in + v_nominal;
}

template <typename Provider>
void Battery<Provider>::commit(SimulationState& st) {
    (void)st;
}

template class Battery<JitProvider>;
