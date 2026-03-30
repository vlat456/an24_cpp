#include "slider.h"
#include "port_registry.h"

template <typename Provider>
void Slider<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Pass control input directly to output (editor pushes value via signal_overrides_)
    float val = st.values[provider.get(PortNames::control)];
    st.values[provider.get(PortNames::out)] = val;
}

template <typename Provider>
void Slider<Provider>::commit(SimulationState& st) {
    (void)st;
}

template class Slider<JitProvider>;
