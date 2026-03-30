#include "indicator_light.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void IndicatorLight<Provider>::execute(SimulationState& st, float /*dt*/) {
    // Push model: brightness derived from v_in directly.
    // In push topology the indicator sits in series and passes voltage through.
    float v_in = st.values[provider.get(PortNames::v_in)];

    // Brightness: normalized input voltage (0..rated → 0..max_brightness)
    float normalized = std::clamp(v_in * inv_rated_voltage, 0.0f, 1.0f);
    st.values[provider.get(PortNames::brightness)] = normalized * max_brightness;

    // Pass-through: downstream sees full input voltage
    st.values[provider.get(PortNames::v_out)] = v_in;
}

template <typename Provider>
void IndicatorLight<Provider>::commit(SimulationState& st) {
    (void)st;
}

template <typename Provider>
void IndicatorLight<Provider>::pre_load() {
    inv_rated_voltage = 1.0f / std::max(rated_voltage, 1e-6f);
}

template class IndicatorLight<JitProvider>;
