#include "positive_v_to_bool.h"
#include "port_registry.h"

template <typename Provider>
void Positive_V_to_Bool<Provider>::execute(SimulationState& st, float /*dt*/) {
    float vin = st.values[provider.get(PortNames::Vin)];
    // Convert positive voltage to TRUE (v > 0)
    bool result = vin > 0.0f;
    st.values[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void Positive_V_to_Bool<Provider>::commit(SimulationState& st, float /*dt*/) {
    (void)st;
}

template class Positive_V_to_Bool<JitProvider>;
