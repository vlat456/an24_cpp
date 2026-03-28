#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// ElectricPump - electric motor driving hydraulic pump
/// Now a two-port hydraulic component (p_in -> p_out) that can form closed loops.
/// Pressure boost is proportional to electrical supply voltage.
/// Electrical draw is load-dependent (proportional to hydraulic flow x pressure).
template <typename Provider = JitProvider>
class ElectricPump {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Hydraulic;

    Provider provider;
    float max_pressure = 1000.0f;

    ElectricPump() = default;

    void solve_electrical(SimulationState& st, float dt);
    void solve_hydraulic(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
