#include "components/all.h"
#include "components/port_registry.h"
#include "../state.h"
#include <cmath>
#include <cstring>

namespace an24 {

// =============================================================================
// Battery
// =============================================================================

template <typename Provider>
void Battery<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_gnd = st.across[provider.get(PortNames::v_in)];
    float v_bus = st.across[provider.get(PortNames::v_out)];
    float g = inv_internal_r;

    float i = (v_nominal + v_gnd - v_bus) * g;
    i = std::clamp(i, -3000.0f, 3000.0f);  // Increased for 115V systems

    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::v_out), provider.get(PortNames::v_in), g);
    st.through[provider.get(PortNames::v_out)] += v_nominal * g;
    st.through[provider.get(PortNames::v_in)] -= v_nominal * g;
}

template <typename Provider>
void Battery<Provider>::pre_load() {
    if (internal_r > 0.0f) {
        inv_internal_r = 1.0f / internal_r;
    }
}

// =============================================================================
// Switch
// =============================================================================

template <typename Provider>
void Switch<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    if (closed && downstream_g > 0.0f) {
        st.conductance[provider.get(PortNames::v_in)] += downstream_g;
        st.through[provider.get(PortNames::v_in)] -= st.across[provider.get(PortNames::v_in)] * downstream_g;
    }
}

template <typename Provider>
void Switch<Provider>::post_step(an24::SimulationState& st, float /*dt*/) {
    float current_control = st.across[provider.get(PortNames::control)];

    if (std::abs(current_control - last_control) > 0.1f) {
        closed = !closed;
    }
    last_control = current_control;

    if (closed) {
        downstream_g = st.conductance[provider.get(PortNames::v_out)];
        st.across[provider.get(PortNames::v_out)] = st.across[provider.get(PortNames::v_in)];
    } else {
        downstream_g = 0.0f;
        st.across[provider.get(PortNames::v_out)] = 0.0f;
    }

    st.across[provider.get(PortNames::state)] = closed ? 1.0f : 0.0f;
}

// =============================================================================
// Relay
// =============================================================================

template <typename Provider>
void Relay<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    v_out_old = st.across[provider.get(PortNames::v_out)];
    if (closed && downstream_g > 0.0f) {
        st.conductance[provider.get(PortNames::v_in)] += downstream_g;
        st.through[provider.get(PortNames::v_in)] += downstream_I - st.across[provider.get(PortNames::v_in)] * downstream_g;
    }
}

template <typename Provider>
void Relay<Provider>::post_step(an24::SimulationState& st, float /*dt*/) {
    float control_voltage = st.across[provider.get(PortNames::control)];
    closed = (control_voltage > hold_threshold);

    if (closed) {
        downstream_g = st.conductance[provider.get(PortNames::v_out)];
        downstream_I = st.through[provider.get(PortNames::v_out)] + v_out_old * st.conductance[provider.get(PortNames::v_out)];
        st.across[provider.get(PortNames::v_out)] = st.across[provider.get(PortNames::v_in)];
    } else {
        downstream_g = 0.0f;
        downstream_I = 0.0f;
        st.across[provider.get(PortNames::v_out)] = 0.0f;
    }
}

// =============================================================================
// HoldButton
// =============================================================================

template <typename Provider>
void HoldButton<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    v_out_old = st.across[provider.get(PortNames::v_out)];
    if (is_pressed && downstream_g > 0.0f) {
        st.conductance[provider.get(PortNames::v_in)] += downstream_g;
        st.through[provider.get(PortNames::v_in)] += downstream_I - st.across[provider.get(PortNames::v_in)] * downstream_g;
    }
}

template <typename Provider>
void HoldButton<Provider>::post_step(an24::SimulationState& st, float /*dt*/) {
    float current = st.across[provider.get(PortNames::control)];

    if (std::abs(current - 1.0f) < 0.1f && std::abs(last_control - 1.0f) >= 0.1f) {
        is_pressed = true;
    } else if (std::abs(current - 2.0f) < 0.1f && std::abs(last_control - 2.0f) >= 0.1f) {
        is_pressed = false;
    }
    last_control = current;

    if (is_pressed) {
        downstream_g = st.conductance[provider.get(PortNames::v_out)];
        downstream_I = st.through[provider.get(PortNames::v_out)] + v_out_old * st.conductance[provider.get(PortNames::v_out)];
        st.across[provider.get(PortNames::v_out)] = st.across[provider.get(PortNames::v_in)];
    } else {
        downstream_g = 0.0f;
        downstream_I = 0.0f;
        st.across[provider.get(PortNames::v_out)] = 0.0f;
    }

    st.across[provider.get(PortNames::state)] = is_pressed ? 1.0f : 0.0f;
}

// =============================================================================
// Resistor
// =============================================================================

template <typename Provider>
void Resistor<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::v_out), provider.get(PortNames::v_in), conductance);
}

// =============================================================================
// Load
// =============================================================================

template <typename Provider>
void Load<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v = st.across[provider.get(PortNames::input)];
    float i = v * conductance;

    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::input), conductance);
}

// =============================================================================
// RefNode
// =============================================================================

template <typename Provider>
void RefNode<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float g = 1.0e6f;
    st.conductance[provider.get(PortNames::v)] += g;
    st.through[provider.get(PortNames::v)] += value * g;
}

// =============================================================================
// Bus
// =============================================================================

template <typename Provider>
void Bus<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // Bus is just a wire - no component behavior
}

// =============================================================================
// BlueprintInput / BlueprintOutput
// =============================================================================

template <typename Provider>
void BlueprintInput<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // No-op - pass-through component (like Bus)
    // Union-find will collapse port to connected signal
}

template <typename Provider>
void BlueprintOutput<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // No-op - pass-through component (like Bus)
    // Union-find will collapse port to connected signal
}

// =============================================================================
// Generator
// =============================================================================

template <typename Provider>
void Generator<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float g = (internal_r > 0.0f) ? 1.0f / internal_r : 0.0f;
    float v_gnd = st.across[provider.get(PortNames::v_in)];
    float v_bus = st.across[provider.get(PortNames::v_out)];

    float i = (v_nominal + v_gnd - v_bus) * g;
    i = std::clamp(i, -1000.0f, 1000.0f);

    st.through[provider.get(PortNames::v_out)] += i;
    st.through[provider.get(PortNames::v_in)] -= i;

    st.conductance[provider.get(PortNames::v_out)] += g;
    st.conductance[provider.get(PortNames::v_in)] += g;
}

// =============================================================================
// GS24
// =============================================================================

template <typename Provider>
void GS24<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_bus = st.across[provider.get(PortNames::v_out)];
    float v_gnd = st.across[provider.get(PortNames::v_in)];
    float rpm_percent = current_rpm / target_rpm;

    if (mode == GS24Mode::STARTER) {
        float back_emf = k_motor * current_rpm;
        float i_consumed = (v_bus - back_emf) / r_internal;

        if (i_consumed < 50.0f) i_consumed = 50.0f;
        if (i_consumed > i_max_starter) i_consumed = i_max_starter;

        float g_internal = 1.0f / r_internal;

        st.through[provider.get(PortNames::v_out)] -= i_consumed;
        st.through[provider.get(PortNames::v_in)] += i_consumed;
        st.conductance[provider.get(PortNames::v_out)] += g_internal;

    } else if (mode == GS24Mode::GENERATOR) {
        float phi = 0.0f;
        if (rpm_percent >= 0.6f) {
            phi = 1.0f;
        } else if (rpm_percent >= rpm_threshold) {
            phi = (rpm_percent - rpm_threshold) / 0.2f;
        }

        float k_mod = (provider.get(PortNames::k_mod) > 0) ? st.across[provider.get(PortNames::k_mod)] : 1.0f;

        float i_no = i_max * phi * k_mod;
        i_no = std::clamp(i_no, 0.0f, 100.0f);

        float g_norton = (r_norton > 0.0f) ? 1.0f / r_norton : 0.0f;

        st.through[provider.get(PortNames::v_out)] += i_no;
        st.conductance[provider.get(PortNames::v_out)] += g_norton;
    }
}

template <typename Provider>
void GS24<Provider>::post_step(an24::SimulationState& st, float dt) {
    (void)st;

    float rpm_percent = current_rpm / target_rpm;

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

// =============================================================================
// Transformer
// =============================================================================

template <typename Provider>
void Transformer<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_primary = st.across[provider.get(PortNames::primary)];
    float v_secondary = v_primary * ratio;

    float g = 1.0f;
    st.conductance[provider.get(PortNames::primary)] += g;
    st.conductance[provider.get(PortNames::secondary)] += g;

    float i = v_secondary * g;
    st.through[provider.get(PortNames::secondary)] += i;
    st.through[provider.get(PortNames::primary)] -= i;
}

// =============================================================================
// Inverter
// =============================================================================

template <typename Provider>
void Inverter<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_dc = st.across[provider.get(PortNames::dc_in)];
    float v_ac = v_dc * efficiency;

    float g = 1.0f;
    st.conductance[provider.get(PortNames::ac_out)] += g;
    st.through[provider.get(PortNames::ac_out)] += v_ac * g;
}

// =============================================================================
// LerpNode
// =============================================================================

template <typename Provider>
void LerpNode<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_input = st.across[provider.get(PortNames::input)];
    float v_output = st.across[provider.get(PortNames::output)];

    float g = 1.0f;
    st.conductance[provider.get(PortNames::output)] += g;

    float i = (v_input - v_output) * factor * g;
    st.through[provider.get(PortNames::output)] += i;
}

template <typename Provider>
void LerpNode<Provider>::post_step(an24::SimulationState& st, float dt) {
    (void)dt;
    float v_input = st.across[provider.get(PortNames::input)];
    float v_output = st.across[provider.get(PortNames::output)];

    float new_output = v_output + factor * (v_input - v_output);
    st.across[provider.get(PortNames::output)] = new_output;
}

// =============================================================================
// PID
// =============================================================================

template <typename Provider>
void PID<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // High-impedance output: the PID drives the output directly in post_step
    // Small conductance keeps the node well-conditioned in the MNA matrix
    st.conductance[provider.get(PortNames::output)] += 1e-6f;
}

template <typename Provider>
void PID<Provider>::post_step(an24::SimulationState& st, float dt) {
    float setpoint = st.across[provider.get(PortNames::setpoint)];
    float feedback = st.across[provider.get(PortNames::feedback)];

    // Clamp dt: branchless MINSS/MAXSS, protects against lag spikes and div-by-zero
    float safe_dt = std::max(1e-6f, std::min(dt, 0.1f));
    float inv_dt  = 1.0f / safe_dt;

    // Error
    float error = setpoint - feedback;

    // P term
    float p_term = Kp * error;

    // I term with clamping anti-windup
    integral += error * safe_dt;
    float i_term = std::clamp(Ki * integral, output_min - p_term, output_max - p_term);

    // D term: first-order low-pass filter on raw derivative
    float d_raw = (error - last_error) * inv_dt;
    d_filtered  += filter_alpha * (d_raw - d_filtered);
    float d_term  = Kd * d_filtered;

    // Output saturation
    float output = std::clamp(p_term + i_term + d_term, output_min, output_max);

    st.across[provider.get(PortNames::output)] = output;
    last_error = error;
}

// =============================================================================
// PD - Proportional-Derivative controller
// =============================================================================

template <typename Provider>
void PD<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // High-impedance output: the PD drives the output directly in post_step
    // Small conductance keeps the node well-conditioned in the MNA matrix
    st.conductance[provider.get(PortNames::output)] += 1e-6f;
}

template <typename Provider>
void PD<Provider>::post_step(an24::SimulationState& st, float dt) {
    float setpoint = st.across[provider.get(PortNames::setpoint)];
    float feedback = st.across[provider.get(PortNames::feedback)];

    // Clamp dt: branchless MINSS/MAXSS, protects against lag spikes and div-by-zero
    float safe_dt = std::max(1e-6f, std::min(dt, 0.1f));
    float inv_dt  = 1.0f / safe_dt;

    // Error
    float error = setpoint - feedback;

    // P term
    float p_term = Kp * error;

    // D term: first-order low-pass filter on raw derivative
    float d_raw = (error - last_error) * inv_dt;
    d_filtered  += filter_alpha * (d_raw - d_filtered);
    float d_term  = Kd * d_filtered;

    // Output saturation (no integral windup concern)
    float output = std::clamp(p_term + d_term, output_min, output_max);

    st.across[provider.get(PortNames::output)] = output;
    last_error = error;
}

// =============================================================================
// PI - Proportional-Integral controller
// =============================================================================

template <typename Provider>
void PI<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // High-impedance output: the PI drives the output directly in post_step
    // Small conductance keeps the node well-conditioned in the MNA matrix
    st.conductance[provider.get(PortNames::output)] += 1e-6f;
}

template <typename Provider>
void PI<Provider>::post_step(an24::SimulationState& st, float dt) {
    float setpoint = st.across[provider.get(PortNames::setpoint)];
    float feedback = st.across[provider.get(PortNames::feedback)];

    // Clamp dt: branchless MINSS/MAXSS, protects against lag spikes and div-by-zero
    float safe_dt = std::max(1e-6f, std::min(dt, 0.1f));

    // Error
    float error = setpoint - feedback;

    // P term
    float p_term = Kp * error;

    // I term with clamping anti-windup
    integral += error * safe_dt;
    float i_term = std::clamp(Ki * integral, output_min - p_term, output_max - p_term);

    // Output saturation
    float output = std::clamp(p_term + i_term, output_min, output_max);

    st.across[provider.get(PortNames::output)] = output;
}

// =============================================================================
// P - Proportional controller
// =============================================================================

template <typename Provider>
void P<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // High-impedance output: the P controller drives the output directly in post_step
    // Small conductance keeps the node well-conditioned in the MNA matrix
    st.conductance[provider.get(PortNames::output)] += 1e-6f;
}

template <typename Provider>
void P<Provider>::post_step(an24::SimulationState& st, float /*dt*/) {
    float setpoint = st.across[provider.get(PortNames::setpoint)];
    float feedback = st.across[provider.get(PortNames::feedback)];

    // Error
    float error = setpoint - feedback;

    // P term (no integral, no derivative)
    float p_term = Kp * error;

    // Output saturation
    float output = std::clamp(p_term, output_min, output_max);

    st.across[provider.get(PortNames::output)] = output;
}

// =============================================================================
// Splitter
// =============================================================================

template <typename Provider>
void Splitter<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Splitter<Provider>::solve_mechanical(an24::SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Splitter<Provider>::solve_hydraulic(an24::SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Splitter<Provider>::solve_thermal(an24::SimulationState& st, float /*dt*/) {}

// =============================================================================
// Merger (inverse of Splitter — 2-to-1, pure alias, no stamping)
// =============================================================================

template <typename Provider>
void Merger<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Merger<Provider>::solve_mechanical(an24::SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Merger<Provider>::solve_hydraulic(an24::SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Merger<Provider>::solve_thermal(an24::SimulationState& st, float /*dt*/) {}

// =============================================================================
// IndicatorLight
// =============================================================================

template <typename Provider>
void IndicatorLight<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::v_in)];
    float v_out = st.across[provider.get(PortNames::v_out)];
    float v_diff = v_in - v_out;

    float g = conductance;
    float i = v_diff * g;

    st.through[provider.get(PortNames::v_out)] += i;
    st.through[provider.get(PortNames::v_in)] -= i;
    st.conductance[provider.get(PortNames::v_out)] += g;
    st.conductance[provider.get(PortNames::v_in)] += g;

    float normalized = std::clamp(v_diff / 28.0f, 0.0f, 1.0f);
    float brightness = normalized * max_brightness;
    st.across[provider.get(PortNames::brightness)] = brightness;
}

// =============================================================================
// HighPowerLoad
// =============================================================================

template <typename Provider>
void HighPowerLoad<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::v_in)];
    float v_out = st.across[provider.get(PortNames::v_out)];
    float v_diff = v_in - v_out;

    if (v_diff > 0.01f) {
        float i = power_draw / v_diff;
        st.through[provider.get(PortNames::v_out)] += i;
        st.through[provider.get(PortNames::v_in)] -= i;

        float g = i / v_diff;
        st.conductance[provider.get(PortNames::v_out)] += g;
        st.conductance[provider.get(PortNames::v_in)] += g;
    }
}

// =============================================================================
// Voltmeter
// =============================================================================

template <typename Provider>
void Voltmeter<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // Voltmeter is purely visual - doesn't affect the circuit
}

// =============================================================================
// Gyroscope & AGK47
// =============================================================================

template <typename Provider>
void Gyroscope<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_input = st.across[provider.get(PortNames::input)];
    st.conductance[provider.get(PortNames::input)] += conductance;
    st.through[provider.get(PortNames::input)] -= v_input * conductance;
}

template <typename Provider>
void AGK47<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_input = st.across[provider.get(PortNames::input)];
    st.conductance[provider.get(PortNames::input)] += conductance;
    st.through[provider.get(PortNames::input)] -= v_input * conductance;
}

// =============================================================================
// ElectricPump
// =============================================================================

template <typename Provider>
void ElectricPump<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::v_in)];
    float g = 0.01f;
    st.conductance[provider.get(PortNames::v_in)] += g;
    st.through[provider.get(PortNames::v_in)] -= v_in * g;
}

template <typename Provider>
void ElectricPump<Provider>::solve_hydraulic(an24::SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::v_in)];
    float p_out = st.across[provider.get(PortNames::p_out)];

    float target_p = v_in * max_pressure / 28.0f;

    float g = 1.0f;
    st.conductance[provider.get(PortNames::p_out)] += g;
    st.through[provider.get(PortNames::p_out)] += target_p * g;
}

// =============================================================================
// SolenoidValve
// =============================================================================

template <typename Provider>
void SolenoidValve<Provider>::solve_hydraulic(an24::SimulationState& st, float /*dt*/) {
    float ctrl = st.across[provider.get(PortNames::ctrl)];
    bool open = (ctrl > 12.0f) ^ normally_closed;

    if (open) {
        float g = 1.0e6f;
        st.conductance[provider.get(PortNames::flow_in)] += g;
        st.conductance[provider.get(PortNames::flow_out)] += g;
    }
}

// =============================================================================
// InertiaNode
// =============================================================================

template <typename Provider>
void InertiaNode<Provider>::solve_mechanical(an24::SimulationState& st, float /*dt*/) {
    float v_input = st.across[provider.get(PortNames::input)];
    float v_output = st.across[provider.get(PortNames::output)];

    float g = damping;
    st.conductance[provider.get(PortNames::output)] += g;

    float i = (v_input - v_output) * inv_mass * g;
    st.through[provider.get(PortNames::output)] += i;
}

template <typename Provider>
void InertiaNode<Provider>::pre_load() {
    if (mass > 0.0f) {
        inv_mass = 1.0f / mass;
    }
}

// =============================================================================
// TempSensor
// =============================================================================

template <typename Provider>
void TempSensor<Provider>::solve_thermal(an24::SimulationState& st, float /*dt*/) {
    float temp_in = st.across[provider.get(PortNames::temp_in)];
    st.across[provider.get(PortNames::temp_out)] = temp_in * sensitivity;
}

// =============================================================================
// ElectricHeater
// =============================================================================

template <typename Provider>
void ElectricHeater<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::power)];
    float g = max_power / (v_in * v_in + 0.01f);
    st.conductance[provider.get(PortNames::power)] += g;
    st.through[provider.get(PortNames::power)] -= v_in * g * efficiency;
}

template <typename Provider>
void ElectricHeater<Provider>::solve_thermal(an24::SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::power)];
    float heat_power = v_in * v_in * efficiency;
    st.through[provider.get(PortNames::heat_out)] += heat_power;
}

// =============================================================================
// RUG82
// =============================================================================

template <typename Provider>
void RUG82<Provider>::solve_electrical(an24::SimulationState& st, float dt) {
    float v_gen = st.across[provider.get(PortNames::v_gen)];

    float error = v_target - v_gen;

    k_mod += kp * error * dt;

    if (k_mod < 0.0f) k_mod = 0.0f;
    if (k_mod > 1.0f) k_mod = 1.0f;

    st.across[provider.get(PortNames::k_mod)] = k_mod;
}

// =============================================================================
// DMR400
// =============================================================================

template <typename Provider>
void DMR400<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    if (!is_closed) {
        return;
    }

    float g_closed = 100.0f;

    st.conductance[provider.get(PortNames::v_gen_ref)] += g_closed;
    st.conductance[provider.get(PortNames::v_out)] += g_closed;

    float v_avg = (st.across[provider.get(PortNames::v_gen_ref)] + st.across[provider.get(PortNames::v_out)]) * 0.5f;
    st.through[provider.get(PortNames::v_gen_ref)] += (v_avg - st.across[provider.get(PortNames::v_gen_ref)]) * g_closed;
    st.through[provider.get(PortNames::v_out)] += (v_avg - st.across[provider.get(PortNames::v_out)]) * g_closed;
}

template <typename Provider>
void DMR400<Provider>::post_step(an24::SimulationState& st, float dt) {
    float v_gen = st.across[provider.get(PortNames::v_gen_ref)];
    float v_bus = st.across[provider.get(PortNames::v_in)];

    if (reconnect_delay > 0.0f) {
        reconnect_delay -= dt;
    }

    if (!is_closed) {
        if (reconnect_delay <= 0.0f && v_gen > v_bus + connect_threshold && v_gen > min_voltage_to_close) {
            is_closed = true;
        }
    } else {
        if (v_bus > v_gen + disconnect_threshold) {
            is_closed = false;
            reconnect_delay = 1.0f;
        }
    }

    st.across[provider.get(PortNames::lamp)] = is_closed ? 0.0f : 1.0f;
}

// =============================================================================
// RU19A
// =============================================================================

template <typename Provider>
void RU19A<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_start = st.across[provider.get(PortNames::v_start)];
    float v_bus = st.across[provider.get(PortNames::v_bus)];

    if (this->state == APUState::OFF) {
        return;
    }

    if (this->state == APUState::CRANKING || this->state == APUState::IGNITION) {
        constexpr float R_START_INTERNAL = 0.025f;
        constexpr float K_MOTOR_BACK_EMF = 38.0f;

        float rpm_percent = current_rpm / target_rpm;
        float back_emf = K_MOTOR_BACK_EMF * rpm_percent;
        float i_consumed = (v_start - back_emf) / R_START_INTERNAL;

        if (i_consumed < 0.0f) i_consumed = 0.0f;
        if (i_consumed > 1000.0f) i_consumed = 1000.0f;

        st.through[provider.get(PortNames::v_start)] -= i_consumed;
        st.conductance[provider.get(PortNames::v_start)] += 1.0f / R_START_INTERNAL;

    } else if (this->state == APUState::RUNNING) {
        float rpm_percent = current_rpm / target_rpm;

        float phi = 0.0f;
        if (rpm_percent >= 0.6f) {
            phi = 1.0f;
        } else if (rpm_percent >= 0.4f) {
            phi = (rpm_percent - 0.4f) / 0.2f;
        }

        float k_mod = st.across[provider.get(PortNames::k_mod)];

        float i_no = 400.0f * phi * k_mod;
        if (i_no > 100.0f) i_no = 100.0f;

        float g_norton = 1.0f / 0.08f;

        st.through[provider.get(PortNames::v_bus)] += i_no;
        st.conductance[provider.get(PortNames::v_bus)] += g_norton;
    }
}

template <typename Provider>
void RU19A<Provider>::solve_mechanical(an24::SimulationState& st, float dt) {
    float target_rpm_local = 0.0f;
    float voltage_factor = 1.0f;

    switch (this->state) {
        case APUState::OFF:
        case APUState::STOPPING:
            target_rpm_local = 0.0f;
            break;
        case APUState::CRANKING: {
            float v_bus = st.across[provider.get(PortNames::v_bus)];
            voltage_factor = std::clamp(v_bus / 24.0f, 0.5f, 1.0f);
            target_rpm_local = 2000.0f * voltage_factor;
            break;
        }
        case APUState::IGNITION:
            target_rpm_local = 5000.0f;
            break;
        case APUState::RUNNING:
            target_rpm_local = target_rpm * 0.6f;
            break;
    }

    float inertia = (target_rpm_local > current_rpm) ? spinup_inertia : spindown_inertia;

    if (target_rpm_local > current_rpm && target_rpm_local > 100.0f) {
        float progress = current_rpm / target_rpm_local;
        float nonlinearity = 1.0f + 3.0f * progress * (1.0f - progress);
        current_rpm += (target_rpm_local - current_rpm) * dt * inertia * nonlinearity;
    } else if (target_rpm_local < current_rpm) {
        current_rpm += (target_rpm_local - current_rpm) * dt * inertia * 2.0f;
    } else {
        current_rpm += (target_rpm_local - current_rpm) * dt * inertia;
    }

    if (current_rpm < 0.0f) current_rpm = 0.0f;
    if (current_rpm > target_rpm) current_rpm = target_rpm;

    st.across[provider.get(PortNames::rpm_out)] = current_rpm;
}

template <typename Provider>
void RU19A<Provider>::solve_thermal(an24::SimulationState& st, float dt) {
    (void)st;

    float target_temp = ambient_temp;

    constexpr float THERMAL_INERTIA_HEATING = 0.2f;
    constexpr float THERMAL_INERTIA_COOLING = 0.1f;

    float inertia = THERMAL_INERTIA_COOLING;

    if (this->state == APUState::IGNITION) {
        target_temp = 150.0f;
        inertia = THERMAL_INERTIA_HEATING;
    } else if (this->state == APUState::RUNNING) {
        target_temp = t4_target;
        inertia = THERMAL_INERTIA_HEATING;
    }

    t4 += (target_temp - t4) * dt * inertia;

    if (t4 > t4_max) {
        this->state = APUState::STOPPING;
    }

    st.across[provider.get(PortNames::t4_out)] = t4;
}

template <typename Provider>
void RU19A<Provider>::post_step(an24::SimulationState& st, float dt) {
    float v_start = st.across[provider.get(PortNames::v_start)];
    float v_bus = st.across[provider.get(PortNames::v_bus)];
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

    float rpm_percent = current_rpm / target_rpm;
    st.across[provider.get(PortNames::rpm_out)] = rpm_percent * 100.0f;
    st.across[provider.get(PortNames::t4_out)] = t4;
}

// =============================================================================
// Radiator
// =============================================================================

template <typename Provider>
void Radiator<Provider>::solve_thermal(an24::SimulationState& st, float /*dt*/) {
    float heat_in = st.across[provider.get(PortNames::heat_in)];
    float heat_out = st.across[provider.get(PortNames::heat_out)];

    float g = cooling_capacity;
    st.conductance[provider.get(PortNames::heat_in)] += g;
    st.conductance[provider.get(PortNames::heat_out)] += g;

    float delta = heat_in - heat_out;
    st.through[provider.get(PortNames::heat_in)] += delta * g;
    st.through[provider.get(PortNames::heat_out)] -= delta * g;
}

// =============================================================================
// Comparator
// =============================================================================

template <typename Provider>
void Comparator<Provider>::pre_load() {
    // Parameters are set by factory from JSON default_params
}

template <typename Provider>
void Comparator<Provider>::solve_logical(an24::SimulationState& st, float /*dt*/) {
    float Va = st.across[provider.get(PortNames::Va)];
    float Vb = st.across[provider.get(PortNames::Vb)];

    float diff = Va - Vb;

    bool set = (diff >= Von);
    bool keep = (diff > Voff);
    output_state = set || (output_state && keep);

    st.across[provider.get(PortNames::o)] = output_state ? 1.0f : 0.0f;
}

template <typename Provider>
void Subtract<Provider>::solve_logical(an24::SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    st.across[provider.get(PortNames::o)] = A - B;
}

// =============================================================================
// Logic Gates
// =============================================================================

template <typename Provider>
void AND<Provider>::solve_logical(an24::SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    // Treat > 0.5V as TRUE, else FALSE
    bool a = (A > 0.5f);
    bool b = (B > 0.5f);
    bool result = a && b;
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void OR<Provider>::solve_logical(an24::SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    bool a = (A > 0.5f);
    bool b = (B > 0.5f);
    bool result = a || b;
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void XOR<Provider>::solve_logical(an24::SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    bool a = (A > 0.5f);
    bool b = (B > 0.5f);
    bool result = a != b;
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void NOT<Provider>::solve_logical(an24::SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    bool a = (A > 0.5f);
    bool result = !a;
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void NAND<Provider>::solve_logical(an24::SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    bool a = (A > 0.5f);
    bool b = (B > 0.5f);
    bool result = !(a && b);
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void Any_V_to_Bool<Provider>::solve_logical(an24::SimulationState& st, float /*dt*/) {
    float vin = st.across[provider.get(PortNames::Vin)];
    // Convert any non-zero voltage to TRUE (including negative) using bit trick
    uint32_t b;
    std::memcpy(&b, &vin, sizeof(b));
    bool result = (b + b) != 0;
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void Positive_V_to_Bool<Provider>::solve_logical(an24::SimulationState& st, float /*dt*/) {
    float vin = st.across[provider.get(PortNames::Vin)];
    // Convert positive voltage to TRUE (v > 0)
    bool result = vin > 0.0f;
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

} // namespace an24

// =============================================================================
// Explicit Template Instantiation for JitProvider
// =============================================================================

#include "explicit_instantiations.h"
