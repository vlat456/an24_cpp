#include "slider.h"
#include "port_registry.h"

template <typename Provider>
void Slider<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    // Pass control input directly to output (editor pushes value via signal_overrides_)
    float val = st.values[provider.get(PortNames::control)];
    st.values[provider.get(PortNames::out)] = val;
}

template <typename Provider>
void Slider<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class Slider<JitProvider>;
