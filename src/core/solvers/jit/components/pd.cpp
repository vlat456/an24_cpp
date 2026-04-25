#include "pd.h"
#include "core/solvers/common/port_names.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void PD<Provider>::execute(SimulationState& st, double dt) {
    constexpr double kDtMin = 1e-6;
    constexpr double kDtMax = 0.1;
    const float safe_dt = static_cast<float>(std::clamp(dt, kDtMin, kDtMax));

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
void PD<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class PD<JitProvider>;
