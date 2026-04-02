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
    APUState next_state = APUState::OFF;
    double timer = 0.0;
    double next_timer = 0.0;
    float target_rpm = 16000.0f;
    double current_rpm = 0.0;
    double next_current_rpm = 0.0;
    float spinup_inertia = 1.0f;
    float spindown_inertia = 0.02f;
    float crank_time = 2.0f;
    float ignition_time = 3.0f;
    float start_timeout = 30.0f;
    double t4 = 0.0;
    double next_t4 = 0.0;
    float t4_target = 400.0f;
    float t4_max = 750.0f;
    float ambient_temp = 20.0f;
    bool auto_start = true;
    bool start_requested = false;
    bool stop_requested = false;

    // Precomputed inverses (set in pre_load)
    float inv_target_rpm = 1.0f / 16000.0f;

    RU19A() = default;

    void start() { start_requested = true; }
    void stop()  { stop_requested = true; }
    bool is_starter_active() const { return state == APUState::CRANKING || state == APUState::IGNITION; }

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
