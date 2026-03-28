#include "voltage_sense.h"
#include "port_registry.h"
#include "../state.h"

template <typename Provider>
void VoltageSense<Provider>::solve_electrical(SimulationState& /*st*/, float /*dt*/) {}

template <typename Provider>
void VoltageSense<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float v = st.values[provider.get(PortNames::v_in)];
    float vref = st.values[provider.get(PortNames::v_ref)];
    st.values[provider.get(PortNames::out)] = (v - vref) * gain + offset;
}

template <typename Provider>
void VoltageSense<Provider>::observe_electrical(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template <typename Provider>
void VoltageSense<Provider>::execute(SimulationState& st, float dt) {
    solve_electrical(st, dt);
    solve_logical(st, dt);
}

template class VoltageSense<JitProvider>;
