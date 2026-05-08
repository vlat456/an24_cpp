#include "p.h"
#include "core/solvers/common/port_names.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void P<Provider>::execute(SimulationState& st, double /*dt*/) {
    float const sp = st.values[provider.get(PortNames::setpoint)];
    float const fb = st.values[provider.get(PortNames::feedback)];
    float const error = sp - fb;
    float output = Kp * error;
    output = std::clamp(output, output_min, output_max);
    st.values[provider.get(PortNames::output)] = output;
}

template <typename Provider>
void P<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class P<JitProvider>;
