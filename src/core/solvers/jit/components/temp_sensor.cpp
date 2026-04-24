#include "temp_sensor.h"
#include "core/solvers/common/port_registry.h"

template <typename Provider>
void TempSensor<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Push model: temperature sensor - pass through with scaling
    float temp_in = st.values[provider.get(PortNames::temp_in)];
    st.values[provider.get(PortNames::temp_out)] = temp_in * sensitivity;
}

template <typename Provider>
void TempSensor<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class TempSensor<JitProvider>;
