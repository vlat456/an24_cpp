#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// RU19A-300 - Auxiliary Power Unit
template <typename Provider = JitProvider>
class RU19A {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Mechanical | Domain::Thermal;

    Provider provider;
    APUState state = APUState::OFF;
    float timer = 0.0f;
    float target_rpm = 16000.0f;
    float current_rpm = 0.0f;
    float spinup_inertia = 1.0f;
    float spindown_inertia = 0.02f;
    float crank_time = 2.0f;
    float ignition_time = 3.0f;
    float runup_time = 8.0f;
    float start_timeout = 30.0f;
    float t4 = 0.0f;
    float t4_target = 400.0f;
    float t4_max = 750.0f;
    float ambient_temp = 20.0f;
    bool auto_start = true;

    // Precomputed inverses (set in pre_load)
    float inv_target_rpm = 1.0f / 16000.0f;

    RU19A() = default;

    void start() { if (state == APUState::OFF) state = APUState::CRANKING; }
    void stop() { state = APUState::STOPPING; }
    bool is_starter_active() const { return state == APUState::CRANKING || state == APUState::IGNITION; }

    void solve_electrical(SimulationState& st, float dt);
    void solve_mechanical(SimulationState& st, float dt);
    void solve_thermal(SimulationState& st, float dt);
    void finalize_step(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load();
};
