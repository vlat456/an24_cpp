#include "indicator_light.h"
#include "core/solvers/common/port_names.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void IndicatorLight<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Observer-style: brightness derived from voltage DROP, but NO pass-through write.
    // Electrical propagation (including v_out) is handled by the electrical solver.
    // The push executor only computes brightness from the already-solved voltages.
    float const v_in  = st.values[provider.get(PortNames::v_in)];
    float const v_out = st.values[provider.get(PortNames::v_out)];

    // Brightness: normalized voltage drop across the component (0..rated → 0..1).
    // Using (v_in - v_out) ensures the indicator only lights when current flows
    // through a complete circuit (v_out connected to ground / return path).
    float const v_drop = v_in - v_out;
    float const normalized = std::clamp(v_drop * inv_rated_voltage, 0.0f, 1.0f);
    st.values[provider.get(PortNames::brightness)] = normalized;
    // NOTE: v_out is NOT written here. It is solved by the electrical solver.
}

template <typename Provider>
void IndicatorLight<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template <typename Provider>
void IndicatorLight<Provider>::pre_load() {
    inv_rated_voltage = 1.0f / std::max(rated_voltage, 1e-6f);
}

template class IndicatorLight<JitProvider>;
