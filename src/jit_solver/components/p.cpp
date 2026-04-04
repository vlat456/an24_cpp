#include "p.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void P<Provider>::execute(SimulationState& st, double /*dt*/) {
    float sp = st.values[provider.get(PortNames::setpoint)];
    float fb = st.values[provider.get(PortNames::feedback)];
    float error = sp - fb;
    float output = Kp * error;
    output = std::clamp(output, output_min, output_max);
    st.values[provider.get(PortNames::output)] = output;
}

template <typename Provider>
void P<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class P<JitProvider>;
