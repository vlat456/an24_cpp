#include "transformer.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void Transformer<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Push model: transformer with voltage ratio
    float v_primary = st.values[provider.get(PortNames::primary)];
    // Secondary voltage = primary voltage * ratio
    float v_secondary_target = v_primary * ratio;
    st.values[provider.get(PortNames::secondary)] = v_secondary_target;
}

template <typename Provider>
void Transformer<Provider>::execute(SimulationState& st, float dt) {
    solve_electrical(st, dt);
}

template class Transformer<JitProvider>;
