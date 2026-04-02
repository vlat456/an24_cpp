#include "dmr400.h"
#include "port_registry.h"

template <typename Provider>
void DMR400<Provider>::execute(SimulationState& st, double dt) {
    float v_gen = st.values[provider.get(PortNames::v_gen_ref)];
    float v_bus = st.values[provider.get(PortNames::v_in)];

    st.values[provider.get(PortNames::v_out)] = is_closed ? v_gen : 0.0f;
    st.values[provider.get(PortNames::lamp)] = is_closed ? 0.0f : 1.0f;

    next_is_closed = is_closed;
    next_reconnect_delay = reconnect_delay;

    if (next_reconnect_delay > 0.0f) {
        next_reconnect_delay -= dt;
    }

    if (!is_closed) {
        if (next_reconnect_delay <= 0.0f && v_gen > v_bus + connect_threshold && v_gen > min_voltage_to_close) {
            next_is_closed = true;
        }
    } else {
        if (v_bus > v_gen + disconnect_threshold) {
            next_is_closed = false;
            next_reconnect_delay = 1.0f;
        }
    }
}

template <typename Provider>
void DMR400<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
    is_closed = next_is_closed;
    reconnect_delay = next_reconnect_delay;
}

template class DMR400<JitProvider>;
