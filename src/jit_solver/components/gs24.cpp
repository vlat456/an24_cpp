#include "gs24.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void GS24<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float rpm_percent = current_rpm * inv_target_rpm;

    // Push model: output voltage based on mode
    // Starter mode: provide voltage on v_out
    // Generator mode: provide voltage on v_out
    
    if (mode == GS24Mode::STARTER || mode == GS24Mode::STARTER_WAIT) {
        // Starter provides no voltage (it's being driven)
        st.values[provider.get(PortNames::v_out)] = 0.0f;
    } else if (mode == GS24Mode::GENERATOR) {
        // Generator provides voltage proportional to RPM
        float phi = std::clamp((rpm_percent - rpm_threshold) * 5.0f, 0.0f, 1.0f);
        float k_mod_val = provider.has(PortNames::k_mod) ? st.values[provider.get(PortNames::k_mod)] : 1.0f;
        float v_out = v_nominal * phi * k_mod_val;
        st.values[provider.get(PortNames::v_out)] = v_out;
    } else {
        // OFF mode
        st.values[provider.get(PortNames::v_out)] = 0.0f;
    }
}

template <typename Provider>
void GS24<Provider>::pre_load() {
    inv_r_internal = 1.0f / std::max(r_internal, 1e-6f);
    inv_r_norton = 1.0f / std::max(r_norton, 1e-6f);
    inv_target_rpm = 1.0f / std::max(target_rpm, 1.0f);
}

template <typename Provider>
void GS24<Provider>::finalize_step(SimulationState& st, float dt) {
    (void)st;

    float rpm_percent = current_rpm * inv_target_rpm;

    switch (mode) {
        case GS24Mode::STARTER:
            if (current_rpm < target_rpm * rpm_cutoff) {
                float acceleration = 300.0f;
                current_rpm += acceleration * dt;
            }

            if (rpm_percent >= rpm_cutoff) {
                current_rpm = target_rpm * rpm_cutoff;
                mode = GS24Mode::STARTER_WAIT;
                wait_time = 0.0f;
            }
            break;

        case GS24Mode::STARTER_WAIT:
            wait_time += dt;
            if (wait_time >= 1.0f) {
                mode = GS24Mode::GENERATOR;
            }
            break;

        case GS24Mode::GENERATOR:
            if (current_rpm < target_rpm) {
                float acceleration = 500.0f;
                current_rpm += acceleration * dt;
                if (current_rpm > target_rpm) current_rpm = target_rpm;
            }
            break;

        case GS24Mode::OFF:
        default:
            break;
    }
}

template <typename Provider>
void GS24<Provider>::execute(SimulationState& st, float dt) {
    solve_electrical(st, dt);
    finalize_step(st, dt);
}

template class GS24<JitProvider>;
