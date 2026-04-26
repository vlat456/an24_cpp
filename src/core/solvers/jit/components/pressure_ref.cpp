#include "pressure_ref.h"
#include "core/solvers/common/port_names.h"
#include "../state.h"

template <typename Provider>
void PressureRef<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Write fixed pressure to signal each frame so downstream consumers
    // see the boundary value. The hydraulic subsolver also stamps this
    // node as a FixedPressureNode — both paths write the same value.
    st.values[provider.get(PortNames::p)] = pressure;
}

template <typename Provider>
void PressureRef<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {}

template class PressureRef<JitProvider>;
