#include "indicator_light.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void IndicatorLight<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Observer-style: brightness derived from v_in, but NO pass-through write.
    // Electrical propagation (including v_out) is handled by the electrical solver.
    // The push executor only computes brightness from the already-solved voltage.
    float v_in = st.values[provider.get(PortNames::v_in)];

    // Brightness: normalized input voltage (0..rated → 0..max_brightness)
    float normalized = std::clamp(v_in * inv_rated_voltage, 0.0f, 1.0f);
    st.values[provider.get(PortNames::brightness)] = normalized * max_brightness;
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
