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
    GS24Mode next_mode = GS24Mode::STARTER;
    double wait_time = 0.0;
    double next_wait_time = 0.0;
    float r_internal = 0.025f;
    float rpm_cutoff = 0.45f;
    float v_nominal = 28.5f;
    float r_norton = 0.08f;
    float target_rpm = 16000.0f;
    double current_rpm = 0.0;
    double next_current_rpm = 0.0;
    float rpm_threshold = 0.4f;

    // Precomputed inverses (set in pre_load)
    float inv_r_internal = 40.0f;   // 1/r_internal
    float inv_r_norton = 12.5f;     // 1/r_norton
    float inv_target_rpm = 1.0f / 16000.0f;

    GS24() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
