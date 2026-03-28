#include "gidro_accumulator.h"
#include "port_registry.h"
#include <algorithm>
#include <cmath>

template <typename Provider>
void GidroAccumulator<Provider>::solve_hydraulic(SimulationState& st, float /*dt*/) {
    float p_in = st.values[provider.get(PortNames::p_in)];
    
    // Boyle's law: P_precharge * V_total = P_gas * V_gas
    // Gas pressure at current gas_volume:
    float p_gas = precharge_pressure * volume / std::max(gas_volume, 0.01f);

    // Push model: output p_out driven toward gas pressure
    // This represents the accumulator releasing fluid when pressure drops
    st.values[provider.get(PortNames::p_out)] = p_gas;
}

template <typename Provider>
void GidroAccumulator<Provider>::finalize_step(SimulationState& st, float dt) {
    float p_in = st.values[provider.get(PortNames::p_in)];
    float p_gas = precharge_pressure * volume / std::max(gas_volume, 0.01f);
    float dp = p_in - p_gas;
    float flow_rate = dp * 0.001f;
    gas_volume = std::clamp(gas_volume - flow_rate * dt, 0.1f, volume);
}

template <typename Provider>
void GidroAccumulator<Provider>::execute(SimulationState& st, float dt) {
    solve_hydraulic(st, dt);
    finalize_step(st, dt);
}

template <typename Provider>
void GidroAccumulator<Provider>::pre_load() {
    gas_volume = std::clamp(gas_volume, 0.1f, volume);
}

template class GidroAccumulator<JitProvider>;
