#include "pi.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void PI<Provider>::execute(SimulationState& st, double dt) {
    constexpr double kDtMin = 1e-6;
    constexpr double kDtMax = 0.1;
    const float safe_dt = static_cast<float>(std::clamp(dt, kDtMin, kDtMax));

    float sp = st.values[provider.get(PortNames::setpoint)];
    float fb = st.values[provider.get(PortNames::feedback)];
    float kp = st.values[provider.get(PortNames::Kp)];
    float ki = st.values[provider.get(PortNames::Ki)];
    float o_min = st.values[provider.get(PortNames::output_min)];
    float o_max = st.values[provider.get(PortNames::output_max)];
    float error = sp - fb;

    integral += error * safe_dt;

    float output = kp * error + ki * integral;
    output = std::clamp(output, o_min, o_max);

    if (std::abs(ki) > 1e-9f) {
        float i_lo = o_min / ki;
        float i_hi = o_max / ki;
        if (i_lo > i_hi) std::swap(i_lo, i_hi);
        integral = std::clamp(integral, static_cast<double>(i_lo), static_cast<double>(i_hi));
    }

    st.values[provider.get(PortNames::output)] = output;
}

template <typename Provider>
void PI<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class PI<JitProvider>;
