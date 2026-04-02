#include "gs24.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

template <typename Provider>
void GS24<Provider>::pre_load() {
    inv_r_internal = 1.0f / std::max(r_internal, 1e-6f);
    inv_r_norton = 1.0f / std::max(r_norton, 1e-6f);
    inv_target_rpm = 1.0f / std::max(target_rpm, 1.0f);
    next_mode = mode;
    next_wait_time = wait_time;
    next_current_rpm = current_rpm;
}

template <typename Provider>
void GS24<Provider>::execute(SimulationState& st, double dt) {
    float rpm_percent = current_rpm * inv_target_rpm;

    if (mode == GS24Mode::STARTER || mode == GS24Mode::STARTER_WAIT) {
        st.values[provider.get(PortNames::v_out)] = 0.0f;
    } else if (mode == GS24Mode::GENERATOR) {
        float phi = std::clamp((rpm_percent - rpm_threshold) * 5.0f, 0.0f, 1.0f);
        float k_mod_val = provider.has(PortNames::k_mod) ? st.values[provider.get(PortNames::k_mod)] : 1.0f;
        float v_out = v_nominal * phi * k_mod_val;
        st.values[provider.get(PortNames::v_out)] = v_out;
    } else {
        st.values[provider.get(PortNames::v_out)] = 0.0f;
    }

    next_mode = mode;
    next_wait_time = wait_time;
    next_current_rpm = current_rpm;

    switch (mode) {
        case GS24Mode::STARTER:
            if (next_current_rpm < target_rpm * rpm_cutoff) {
                float acceleration = 300.0f;
                next_current_rpm += acceleration * dt;
            }

            if (next_current_rpm * inv_target_rpm >= rpm_cutoff) {
                next_current_rpm = target_rpm * rpm_cutoff;
                next_mode = GS24Mode::STARTER_WAIT;
                next_wait_time = 0.0f;
            }
            break;

        case GS24Mode::STARTER_WAIT:
            next_wait_time += dt;
            if (next_wait_time >= 1.0f) {
                next_mode = GS24Mode::GENERATOR;
            }
            break;

        case GS24Mode::GENERATOR:
            if (next_current_rpm < target_rpm) {
                float acceleration = 500.0f;
                next_current_rpm += acceleration * dt;
                if (next_current_rpm > target_rpm) {
                    next_current_rpm = target_rpm;
                }
            }
            break;

        case GS24Mode::OFF:
        default:
            break;
    }
}

template <typename Provider>
void GS24<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
    mode = next_mode;
    wait_time = next_wait_time;
    current_rpm = next_current_rpm;
}

template class GS24<JitProvider>;
