#include "ru19a.h"
#include "port_registry.h"
#include <algorithm>

template <typename Provider>
void RU19A<Provider>::execute(SimulationState& st, double dt) {
    float v_start_input = st.values[provider.get(PortNames::v_start)];

    float rpm_percent = current_rpm * inv_target_rpm;

    if (state == APUState::CRANKING || state == APUState::IGNITION) {
        st.values[provider.get(PortNames::v_start)] = 0.0f;
        st.values[provider.get(PortNames::v_bus)] = 0.0f;
    } else if (state == APUState::RUNNING) {
        float phi = std::clamp((rpm_percent - 0.4f) * 5.0f, 0.0f, 1.0f);
        float k_mod = st.values[provider.get(PortNames::k_mod)];
        float v_out = 28.5f * phi * k_mod;
        st.values[provider.get(PortNames::v_bus)] = v_out;
    } else {
        st.values[provider.get(PortNames::v_start)] = 0.0f;
        st.values[provider.get(PortNames::v_bus)] = 0.0f;
    }

    next_state = state;
    next_timer = timer + dt;
    next_current_rpm = current_rpm;
    next_t4 = t4;

    if (stop_requested) {
        if (state != APUState::OFF && state != APUState::STOPPING) {
            next_state = APUState::STOPPING;
        }
        stop_requested = false;
    }

    if (start_requested) {
        if (state == APUState::OFF) {
            next_state = APUState::CRANKING;
            next_timer = 0.0f;
        }
        start_requested = false;
    }

    switch (state) {
        case APUState::OFF: {
            next_t4 = ambient_temp;
            next_timer = 0.0f;

            if (auto_start && v_start_input > 10.0f) {
                next_state = APUState::CRANKING;
            }
            break;
        }

        case APUState::CRANKING: {
            if (next_timer >= crank_time) {
                next_state = APUState::IGNITION;
                next_timer = 0.0f;
            }
            break;
        }

        case APUState::IGNITION: {
            if (next_timer > start_timeout) {
                next_state = APUState::STOPPING;
            } else if (next_timer >= ignition_time) {
                next_state = APUState::RUNNING;
                next_timer = 0.0f;
            }
            break;
        }

        case APUState::RUNNING: {
            break;
        }

        case APUState::STOPPING: {
            if (next_current_rpm <= 0.1f) {
                next_current_rpm = 0.0f;
                next_state = APUState::OFF;
            }
            break;
        }
    }

    // Mechanical dynamics
    {
        float drive_voltage = v_start_input;
        if (state == APUState::RUNNING) {
            drive_voltage = st.values[provider.get(PortNames::v_bus)];
        }

        float off_mask = (state == APUState::OFF || state == APUState::STOPPING) ? 1.0f : 0.0f;
        float crank_mask = (state == APUState::CRANKING) ? 1.0f : 0.0f;
        float ign_mask = (state == APUState::IGNITION) ? 1.0f : 0.0f;
        float run_mask = (state == APUState::RUNNING) ? 1.0f : 0.0f;

        float voltage_factor = std::clamp(drive_voltage * (1.0f / 24.0f), 0.5f, 1.0f);
        float target_rpm_local = 0.0f * off_mask
                               + 2000.0f * voltage_factor * crank_mask
                               + 5000.0f * ign_mask
                               + target_rpm * 0.6f * run_mask;

        float inertia = (target_rpm_local > next_current_rpm) ? spinup_inertia : spindown_inertia;

        float accel_mask = (target_rpm_local > next_current_rpm && target_rpm_local > 100.0f) ? 1.0f : 0.0f;
        float decel_mask = (target_rpm_local < next_current_rpm) ? 1.0f : 0.0f;
        float hold_mask = 1.0f - accel_mask - decel_mask;

        float safe_target_local = std::max(target_rpm_local, 1.0f);
        float progress = next_current_rpm / safe_target_local;
        float nonlinearity = 1.0f + 3.0f * progress * (1.0f - progress);

        float diff = target_rpm_local - next_current_rpm;
        float delta = diff * dt * inertia;

        float change = delta * nonlinearity * accel_mask
                     + delta * 2.0f * decel_mask
                     + delta * hold_mask;

        next_current_rpm += change;

        next_current_rpm = std::clamp(next_current_rpm, 0.0, static_cast<double>(target_rpm));
    }

    // Thermal dynamics
    {
        constexpr float THERMAL_INERTIA_HEATING = 0.2f;
        constexpr float THERMAL_INERTIA_COOLING = 0.1f;

        float ign_mask = (state == APUState::IGNITION) ? 1.0f : 0.0f;
        float run_mask = (state == APUState::RUNNING) ? 1.0f : 0.0f;
        float heat_mask = std::max(ign_mask, run_mask);
        float cool_mask = 1.0f - heat_mask;

        float target_temp = ambient_temp * cool_mask + 150.0f * ign_mask + t4_target * run_mask;

        float inertia = THERMAL_INERTIA_HEATING * heat_mask + THERMAL_INERTIA_COOLING * cool_mask;

        next_t4 += (target_temp - next_t4) * dt * inertia;

        if (next_t4 > t4_max) {
            next_state = APUState::STOPPING;
        }
    }

    st.values[provider.get(PortNames::rpm_out)] = (current_rpm * inv_target_rpm) * 100.0f;
    st.values[provider.get(PortNames::t4_out)] = t4;
}

template <typename Provider>
void RU19A<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
    state = next_state;
    timer = next_timer;
    current_rpm = next_current_rpm;
    t4 = next_t4;
}

template <typename Provider>
void RU19A<Provider>::pre_load() {
    inv_target_rpm = 1.0f / std::max(target_rpm, 1.0f);
    next_state = state;
    next_timer = timer;
    next_current_rpm = current_rpm;
    next_t4 = t4;
}

template class RU19A<JitProvider>;
