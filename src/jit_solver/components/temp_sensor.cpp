#include "temp_sensor.h"
#include "port_registry.h"

template <typename Provider>
void TempSensor<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Push model: temperature sensor - pass through with scaling
    float temp_in = st.values[provider.get(PortNames::temp_in)];
    st.values[provider.get(PortNames::temp_out)] = temp_in * sensitivity;
}

template <typename Provider>
void TempSensor<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class TempSensor<JitProvider>;
