#include "solenoid_valve.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void SolenoidValve<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Push model: valve passes through when open, blocks when closed
    float ctrl = st.values[provider.get(PortNames::ctrl)];
    float ctrl_above = (ctrl > 12.0f) ? 1.0f : 0.0f;
    float no_mask = normally_closed ? 0.0f : 1.0f;
    open_mask = std::abs(ctrl_above - no_mask);
    
    if (open_mask > 0.5f) {
        // Valve is open - pass through flow
        float flow_in = st.values[provider.get(PortNames::flow_in)];
        st.values[provider.get(PortNames::flow_out)] = flow_in;
    } else {
        // Valve is closed - no flow
        st.values[provider.get(PortNames::flow_out)] = 0.0f;
    }
}

template <typename Provider>
void SolenoidValve<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class SolenoidValve<JitProvider>;
