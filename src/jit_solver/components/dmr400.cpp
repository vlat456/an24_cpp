#include "dmr400.h"
#include "port_registry.h"

template <typename Provider>
void DMR400<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Push model: when closed, propagate v_gen_ref to v_out; when open, set v_out=0
    if (is_closed) {
        float v_gen = st.values[provider.get(PortNames::v_gen_ref)];
        st.values[provider.get(PortNames::v_out)] = v_gen;
    } else {
        st.values[provider.get(PortNames::v_out)] = 0.0f;
    }
}

template <typename Provider>
void DMR400<Provider>::finalize_step(SimulationState& st, float dt) {
    float v_gen = st.values[provider.get(PortNames::v_gen_ref)];
    float v_bus = st.values[provider.get(PortNames::v_in)];

    if (reconnect_delay > 0.0f) {
        reconnect_delay -= dt;
    }

    if (!is_closed) {
        if (reconnect_delay <= 0.0f && v_gen > v_bus + connect_threshold && v_gen > min_voltage_to_close) {
            is_closed = true;
        }
    } else {
        if (v_bus > v_gen + disconnect_threshold) {
            is_closed = false;
            reconnect_delay = 1.0f;
        }
    }

    st.values[provider.get(PortNames::lamp)] = is_closed ? 0.0f : 1.0f;
}

template <typename Provider>
void DMR400<Provider>::execute(SimulationState& st, float dt) {
    solve_electrical(st, dt);
    finalize_step(st, dt);
}

template class DMR400<JitProvider>;
