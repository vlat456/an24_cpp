#include "pneumatic_compressor.h"
#include "core/solvers/common/port_names.h"
#include <algorithm>
#include <cmath>

template <typename Provider>
void PneumaticCompressor<Provider>::execute(SimulationState& st, double /*dt*/) {
    float rpm = st.values[provider.get(PortNames::rpm_in)];

    // Centrifugal compressor characteristic: P ∝ RPM²
    // Normalized: P_out = max_pressure * (rpm / rated_rpm)²
    float rpm_frac = std::clamp(rpm / rated_rpm, 0.0f, 1.5f);  // allow some overspeed
    float pressure = max_pressure * rpm_frac * rpm_frac;

    // Write to p_source — the CopySignal patch op copies this to element_value_a
    st.values[provider.get(PortNames::p_source)] = pressure;
}

template <typename Provider>
void PneumaticCompressor<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    // No state transitions for compressor
}

template class PneumaticCompressor<JitProvider>;
