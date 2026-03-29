#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// DMR400 - Differential Minimum Relay
template <typename Provider = JitProvider>
class DMR400 {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    bool is_closed = false;
    bool next_is_closed = false;
    float connect_threshold = 2.0f;
    float disconnect_threshold = 10.0f;
    float min_voltage_to_close = 20.0f;
    float reconnect_delay = 0.0f;
    float next_reconnect_delay = 0.0f;

    DMR400() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {
        next_is_closed = is_closed;
        next_reconnect_delay = reconnect_delay;
    }
};
