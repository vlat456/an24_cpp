#include "pid.h"
#include "core/solvers/common/port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void PID<Provider>::execute(SimulationState& st, double dt) {
    constexpr double kDtMin = 1e-6;
    constexpr double kDtMax = 0.1;
    const float safe_dt = static_cast<float>(std::clamp(dt, kDtMin, kDtMax));

    float sp = st.values[provider.get(PortNames::setpoint)];
    float fb = st.values[provider.get(PortNames::feedback)];
    float error = sp - fb;

    integral += error * safe_dt;

    float derivative = (error - last_error) / safe_dt;
    d_filtered += filter_alpha * (derivative - d_filtered);

    float output = Kp * error + Ki * integral + Kd * d_filtered;
    output = std::clamp(output, output_min, output_max);

    if (std::abs(Ki) > 1e-9f) {
        float i_lo = output_min / Ki;
        float i_hi = output_max / Ki;
        if (i_lo > i_hi) std::swap(i_lo, i_hi);
        integral = std::clamp(integral, static_cast<double>(i_lo), static_cast<double>(i_hi));
    }

    st.values[provider.get(PortNames::output)] = output;
    last_error = error;
}

template <typename Provider>
void PID<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class PID<JitProvider>;
