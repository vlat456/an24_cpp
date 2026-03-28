#include "p.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void P<Provider>::solve_logical(SimulationState& st, float dt) {
    (void)dt;
    float sp = st.values[provider.get(PortNames::setpoint)];
    float fb = st.values[provider.get(PortNames::feedback)];
    float error = sp - fb;
    float output = Kp * error;
    output = std::clamp(output, output_min, output_max);
    st.values[provider.get(PortNames::output)] = output;
}

template <typename Provider>
void P<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class P<JitProvider>;
