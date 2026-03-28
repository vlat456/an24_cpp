#include "ru19a.h"
#include "port_registry.h"
#include <algorithm>

template <typename Provider>
void RU19A<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float rpm_percent = current_rpm * inv_target_rpm;

    // Push model: output voltage based on mode
    if (state == APUState::CRANKING || state == APUState::IGNITION) {
        // Starter provides no voltage
        st.values[provider.get(PortNames::v_start)] = 0.0f;
        st.values[provider.get(PortNames::v_bus)] = 0.0f;
    } else if (state == APUState::RUNNING) {
        // Generator provides voltage proportional to RPM
        float phi = std::clamp((rpm_percent - 0.4f) * 5.0f, 0.0f, 1.0f);
        float k_mod = st.values[provider.get(PortNames::k_mod)];
        float v_out = 28.5f * phi * k_mod;
        st.values[provider.get(PortNames::v_bus)] = v_out;
    } else {
        // OFF or STOPPING
        st.values[provider.get(PortNames::v_start)] = 0.0f;
        st.values[provider.get(PortNames::v_bus)] = 0.0f;
    }
}

template <typename Provider>
void RU19A<Provider>::solve_mechanical(SimulationState& st, float dt) {
    float v_bus = st.values[provider.get(PortNames::v_bus)];

    // Branchless state masks
    float off_mask = (this->state == APUState::OFF || this->state == APUState::STOPPING) ? 1.0f : 0.0f;
    float crank_mask = (this->state == APUState::CRANKING) ? 1.0f : 0.0f;
    float ign_mask = (this->state == APUState::IGNITION) ? 1.0f : 0.0f;
    float run_mask = (this->state == APUState::RUNNING) ? 1.0f : 0.0f;

    // Compute target RPM for each state, combine via masks
    float voltage_factor = std::clamp(v_bus * (1.0f / 24.0f), 0.5f, 1.0f);
    float target_rpm_local = 0.0f * off_mask
                           + 2000.0f * voltage_factor * crank_mask
                           + 5000.0f * ign_mask
                           + target_rpm * 0.6f * run_mask;

    // Branchless inertia select
    float inertia = (target_rpm_local > current_rpm) ? spinup_inertia : spindown_inertia;

    // Branchless nonlinearity: apply spinup curve when accelerating above 100 RPM
    float accel_mask = (target_rpm_local > current_rpm && target_rpm_local > 100.0f) ? 1.0f : 0.0f;
    float decel_mask = (target_rpm_local < current_rpm) ? 1.0f : 0.0f;
    float hold_mask = 1.0f - accel_mask - decel_mask;

    // Safe division for progress (only matters when accel_mask == 1, target > 100)
    float safe_target_local = std::max(target_rpm_local, 1.0f);
    float progress = current_rpm / safe_target_local;
    float nonlinearity = 1.0f + 3.0f * progress * (1.0f - progress);

    float diff = target_rpm_local - current_rpm;
    float delta = diff * dt * inertia;

    float change = delta * nonlinearity * accel_mask
                 + delta * 2.0f * decel_mask
                 + delta * hold_mask;

    current_rpm += change;

    // Branchless clamp
    current_rpm = std::clamp(current_rpm, 0.0f, target_rpm);
}

template <typename Provider>
void RU19A<Provider>::solve_thermal(SimulationState& st, float dt) {
    constexpr float THERMAL_INERTIA_HEATING = 0.2f;
    constexpr float THERMAL_INERTIA_COOLING = 0.1f;

    // Branchless state masks
    float ign_mask = (this->state == APUState::IGNITION) ? 1.0f : 0.0f;
    float run_mask = (this->state == APUState::RUNNING) ? 1.0f : 0.0f;
    float heat_mask = std::max(ign_mask, run_mask); // heating if ignition or running
    float cool_mask = 1.0f - heat_mask;

    // Target temperature: ambient when cooling, 150 for ignition, t4_target for running
    float target_temp = ambient_temp * cool_mask + 150.0f * ign_mask + t4_target * run_mask;

    // Inertia: heating rate when hot states, cooling rate otherwise
    float inertia = THERMAL_INERTIA_HEATING * heat_mask + THERMAL_INERTIA_COOLING * cool_mask;

    t4 += (target_temp - t4) * dt * inertia;

    // Thermal overtemp protection (state transition stays branchy — runs at 1 Hz)
    if (t4 > t4_max) {
        this->state = APUState::STOPPING;
    }

    st.values[provider.get(PortNames::t4_out)] = t4;
}

template <typename Provider>
void RU19A<Provider>::finalize_step(SimulationState& st, float dt) {
    float v_start = st.values[provider.get(PortNames::v_start)];
    (void)v_start;
    float v_bus = st.values[provider.get(PortNames::v_bus)];
    (void)v_bus;
    timer += dt;

    switch (this->state) {
        case APUState::OFF: {
            t4 = ambient_temp;
            timer = 0.0f;

            if (auto_start && v_start > 10.0f) {
                this->state = APUState::CRANKING;
            }
            break;
        }

        case APUState::CRANKING: {
            if (timer >= crank_time) {
                this->state = APUState::IGNITION;
                timer = 0.0f;
            }
            break;
        }

        case APUState::IGNITION: {
            if (timer >= ignition_time) {
                this->state = APUState::RUNNING;
                timer = 0.0f;
            }

            if (timer > start_timeout) {
                this->state = APUState::STOPPING;
            }
            break;
        }

        case APUState::RUNNING: {
            break;
        }

        case APUState::STOPPING: {
            if (current_rpm <= 0.1f) {
                current_rpm = 0.0f;
                this->state = APUState::OFF;
            }
            break;
        }
    }

    float rpm_percent = current_rpm * inv_target_rpm;
    st.values[provider.get(PortNames::rpm_out)] = rpm_percent * 100.0f;
    st.values[provider.get(PortNames::t4_out)] = t4;
}

template <typename Provider>
void RU19A<Provider>::execute(SimulationState& st, float dt) {
    solve_electrical(st, dt);
    solve_mechanical(st, dt);
    solve_thermal(st, dt);
    finalize_step(st, dt);
}

template <typename Provider>
void RU19A<Provider>::pre_load() {
    inv_target_rpm = 1.0f / std::max(target_rpm, 1.0f);
}

template class RU19A<JitProvider>;
