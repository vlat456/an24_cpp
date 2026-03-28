#include "pd.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void PD<Provider>::solve_logical(SimulationState& st, float dt) {
    constexpr float kDtMin = 1e-6f;
    constexpr float kDtMax = 0.1f;
    const float safe_dt = std::clamp(dt, kDtMin, kDtMax);

    float sp = st.values[provider.get(PortNames::setpoint)];
    float fb = st.values[provider.get(PortNames::feedback)];
    float error = sp - fb;

    float derivative = (error - last_error) / safe_dt;
    d_filtered += filter_alpha * (derivative - d_filtered);

    float output = Kp * error + Kd * d_filtered;
    output = std::clamp(output, output_min, output_max);

    st.values[provider.get(PortNames::output)] = output;
    last_error = error;
}

template <typename Provider>
void PD<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class PD<JitProvider>;
