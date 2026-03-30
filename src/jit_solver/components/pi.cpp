#include "pi.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void PI<Provider>::execute(SimulationState& st, float dt) {
    constexpr float kDtMin = 1e-6f;
    constexpr float kDtMax = 0.1f;
    const float safe_dt = std::clamp(dt, kDtMin, kDtMax);

    float sp = st.values[provider.get(PortNames::setpoint)];
    float fb = st.values[provider.get(PortNames::feedback)];
    float error = sp - fb;

    integral += error * safe_dt;

    float output = Kp * error + Ki * integral;
    output = std::clamp(output, output_min, output_max);

    if (std::abs(Ki) > 1e-9f) {
        float i_lo = output_min / Ki;
        float i_hi = output_max / Ki;
        if (i_lo > i_hi) std::swap(i_lo, i_hi);
        integral = std::clamp(integral, i_lo, i_hi);
    }

    st.values[provider.get(PortNames::output)] = output;
}

template <typename Provider>
void PI<Provider>::commit(SimulationState& st) {
    (void)st;
}

template class PI<JitProvider>;
