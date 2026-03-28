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
    float connect_threshold = 2.0f;
    float disconnect_threshold = 10.0f;
    float min_voltage_to_close = 20.0f;
    float reconnect_delay = 0.0f;

    DMR400() = default;

    void solve_electrical(SimulationState& st, float dt);
    void finalize_step(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
