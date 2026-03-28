#include "pid.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void PID<Provider>::solve_logical(SimulationState& st, float dt) {
    constexpr float kDtMin = 1e-6f;
    constexpr float kDtMax = 0.1f;
    const float safe_dt = std::clamp(dt, kDtMin, kDtMax);

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
        integral = std::clamp(integral, i_lo, i_hi);
    }

    st.values[provider.get(PortNames::output)] = output;
    last_error = error;
}

template <typename Provider>
void PID<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class PID<JitProvider>;
