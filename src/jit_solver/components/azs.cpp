#include "azs.h"
#include "port_registry.h"
#include <cmath>

template <typename Provider>
void AZS<Provider>::pre_load() {
    if (i_nominal > 0.0f) {
        r_heat = 1.0f / (i_nominal * i_nominal);
    }
}

template <typename Provider>
void AZS<Provider>::commit_control(SimulationState& st, float dt) {
    (void)dt;
    float current_control = st.values[provider.get(PortNames::control)];
    if (std::abs(current_control - last_control) > 0.1f) {
        if (!closed) tripped = false;
        closed = !closed;
    }
    last_control = current_control;

    // Thermal trip
    if (closed && temp > 1.0f) {
        closed = false;
        tripped = true;
    }

    // Output state signals
    st.values[provider.get(PortNames::state)] = closed ? 1.0f : 0.0f;
    st.values[provider.get(PortNames::temp)] = temp;
    st.values[provider.get(PortNames::tripped)] = tripped ? 1.0f : 0.0f;
}

template <typename Provider>
void AZS<Provider>::execute(SimulationState& st, float dt) {
    // Electrical behavior: when closed, propagate v_in to v_out; when open, set v_out=0.
    if (closed) {
        float v_in = st.values[provider.get(PortNames::v_in)];
        st.values[provider.get(PortNames::v_out)] = v_in;
        // Simple current estimate: I = V / R (using i_nominal to derive R)
        float r = (i_nominal > 0.0f) ? (28.0f / i_nominal) : 1.0f;
        current = (v_in > 0.0f) ? (v_in / r) : 0.0f;
    } else {
        st.values[provider.get(PortNames::v_out)] = 0.0f;
        current = 0.0f;
    }
    
    // Thermal behavior: use current computed above.
    // T += (I² * r_heat - T * k_cool) * dt
    float I = current;
    temp += (I * I * r_heat - temp * k_cool) * dt;
    // Floor at zero
    temp = std::max(temp, 0.0f);
}

template <typename Provider>
void AZS<Provider>::commit(SimulationState& st) {
    commit_control(st, 0.0f);
}

template class AZS<JitProvider>;
