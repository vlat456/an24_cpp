#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// GS24 - Starter-Generator
template <typename Provider = JitProvider>
class GS24 {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    GS24Mode mode = GS24Mode::STARTER;
    float start_time = 0.0f;
    float wait_time = 0.0f;
    float r_internal = 0.025f;
    float k_motor = 0.5f;
    float i_max_starter = 800.0f;
    float rpm_cutoff = 0.45f;
    float v_nominal = 28.5f;
    float r_norton = 0.08f;
    float target_rpm = 15000.0f;
    float current_rpm = 0.0f;
    float i_max = 400.0f;
    float rpm_threshold = 0.4f;

    // Precomputed inverses (set in pre_load)
    float inv_r_internal = 40.0f;   // 1/r_internal
    float inv_r_norton = 12.5f;     // 1/r_norton
    float inv_target_rpm = 1.0f / 15000.0f;

    GS24() = default;

    void solve_electrical(SimulationState& st, float dt);
    void finalize_step(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load();
};
