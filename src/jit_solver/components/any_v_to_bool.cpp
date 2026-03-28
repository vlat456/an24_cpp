#include "any_v_to_bool.h"
#include "port_registry.h"

template <typename Provider>
void Any_V_to_Bool<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float vin = st.values[provider.get(PortNames::Vin)];
    bool result = (vin > 0.5f);
    st.values[provider.get(PortNames::out)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void Any_V_to_Bool<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class Any_V_to_Bool<JitProvider>;
