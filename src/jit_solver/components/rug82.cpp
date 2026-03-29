#include "rug82.h"
#include "port_registry.h"
#include <algorithm>

template <typename Provider>
void RUG82<Provider>::execute(SimulationState& st, float dt) {
    st.values[provider.get(PortNames::k_mod)] = k_mod;

    float v_gen = st.values[provider.get(PortNames::v_gen)];
    float error = v_target - v_gen;
    next_k_mod = std::clamp(k_mod + kp * error * dt, 0.0f, 1.0f);
}

template <typename Provider>
void RUG82<Provider>::commit(SimulationState& st) {
    (void)st;
    k_mod = next_k_mod;
}

template class RUG82<JitProvider>;
