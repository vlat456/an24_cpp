#include "slider.h"
#include "core/solvers/common/port_registry.h"

template <typename Provider>
void Slider<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Pass control input directly to output (editor pushes value via signal_overrides_)
    float val = st.values[provider.get(PortNames::control)];
    st.values[provider.get(PortNames::out)] = val;
}

template <typename Provider>
void Slider<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Slider<JitProvider>;
