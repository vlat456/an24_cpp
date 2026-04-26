#include "pneumatic_ref.h"
#include "core/solvers/common/port_names.h"

template <typename Provider>
void PneumaticRef<Provider>::execute(SimulationState& st, double /*dt*/) {
    st.values[provider.get(PortNames::p)] = pressure;
}

template <typename Provider>
void PneumaticRef<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    // No state transitions
}

template class PneumaticRef<JitProvider>;
