#include "rug82.h"
#include "port_registry.h"
#include <algorithm>

template <typename Provider>
void RUG82<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Push model: output current k_mod value
    st.values[provider.get(PortNames::k_mod)] = k_mod;
}

template <typename Provider>
void RUG82<Provider>::finalize_step(SimulationState& st, float dt) {
    float v_gen = st.values[provider.get(PortNames::v_gen)];
    float error = v_target - v_gen;
    k_mod += kp * error * dt;
    k_mod = std::clamp(k_mod, 0.0f, 1.0f);
    st.values[provider.get(PortNames::k_mod)] = k_mod;
}

template <typename Provider>
void RUG82<Provider>::execute(SimulationState& st, float dt) {
    solve_electrical(st, dt);
    finalize_step(st, dt);
}

template class RUG82<JitProvider>;
