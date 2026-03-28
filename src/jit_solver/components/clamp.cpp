#include "clamp.h"
#include "port_registry.h"
#include <algorithm>

template <typename Provider>
void Clamp<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);

    float input = st.values[in_idx];

    // std::clamp compiles to f32.min/f32.max in WASM
    st.values[out_idx] = std::clamp(input, min, max);
}

template <typename Provider>
void Clamp<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class Clamp<JitProvider>;
