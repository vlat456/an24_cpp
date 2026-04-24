#include "gidro_accumulator.h"
#include "core/solvers/common/port_registry.h"
#include <algorithm>
#include <cmath>

template <typename Provider>
void GidroAccumulator<Provider>::execute(SimulationState& st, double dt) {
    float p_in = st.values[provider.get(PortNames::p_in)];
    
    // Boyle's law: P_precharge * V_total = P_gas * V_gas
    // Gas pressure at current gas_volume:
    float p_gas = precharge_pressure * volume / static_cast<float>(std::max(gas_volume, 0.01));

    // Push model: output p_out driven toward gas pressure
    // This represents the accumulator releasing fluid when pressure drops
    st.values[provider.get(PortNames::p_out)] = p_gas;

    // Finalize: update gas volume based on pressure difference
    float dp = p_in - p_gas;
    float flow_rate = dp * 0.001f;
    gas_volume = std::clamp(gas_volume - static_cast<double>(flow_rate) * dt, 0.1, static_cast<double>(volume));
}

template <typename Provider>
void GidroAccumulator<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template <typename Provider>
void GidroAccumulator<Provider>::pre_load() {
    gas_volume = std::clamp(gas_volume, 0.1, static_cast<double>(volume));
}

template class GidroAccumulator<JitProvider>;
