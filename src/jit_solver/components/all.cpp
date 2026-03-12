#include "components/all.h"
#include "components/port_registry.h"
#include "../state.h"
#include <cmath>
#include <cstring>

// =============================================================================
// Battery
// =============================================================================

template <typename Provider>
void Battery<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
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
void Switch<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    if (closed && downstream_g > 0.0f) {
        st.conductance[provider.get(PortNames::v_in)] += downstream_g;
        st.through[provider.get(PortNames::v_in)] -= st.across[provider.get(PortNames::v_in)] * downstream_g;
    }
}

template <typename Provider>
void Switch<Provider>::post_step(SimulationState& st, float /*dt*/) {
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
void Relay<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    v_out_old = st.across[provider.get(PortNames::v_out)];
    if (closed && downstream_g > 0.0f) {
        st.conductance[provider.get(PortNames::v_in)] += downstream_g;
        st.through[provider.get(PortNames::v_in)] += downstream_I - st.across[provider.get(PortNames::v_in)] * downstream_g;
    }
}

template <typename Provider>
void Relay<Provider>::post_step(SimulationState& st, float /*dt*/) {
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
void HoldButton<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    v_out_old = st.across[provider.get(PortNames::v_out)];
    if (is_pressed && downstream_g > 0.0f) {
        st.conductance[provider.get(PortNames::v_in)] += downstream_g;
        st.through[provider.get(PortNames::v_in)] += downstream_I - st.across[provider.get(PortNames::v_in)] * downstream_g;
    }
}

template <typename Provider>
void HoldButton<Provider>::post_step(SimulationState& st, float /*dt*/) {
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
void Resistor<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::v_out), provider.get(PortNames::v_in), conductance);
}

// =============================================================================
// Load
// =============================================================================

template <typename Provider>
void Load<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v = st.across[provider.get(PortNames::input)];
    float i = v * conductance;

    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::input), conductance);
}

// =============================================================================
// RefNode
// =============================================================================

template <typename Provider>
void RefNode<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float g = 1.0e6f;
    st.conductance[provider.get(PortNames::v)] += g;
    st.through[provider.get(PortNames::v)] += value * g;
}

// =============================================================================
// Bus
// =============================================================================

template <typename Provider>
void Bus<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Bus is just a wire - no component behavior
}

// =============================================================================
// BlueprintInput / BlueprintOutput
// =============================================================================

template <typename Provider>
void BlueprintInput<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // No-op - pass-through component (like Bus)
    // Union-find will collapse port to connected signal
}

template <typename Provider>
void BlueprintOutput<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // No-op - pass-through component (like Bus)
    // Union-find will collapse port to connected signal
}

// =============================================================================
// Generator
// =============================================================================

template <typename Provider>
void Generator<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float g = inv_internal_r;
    float v_gnd = st.across[provider.get(PortNames::v_in)];
    float v_bus = st.across[provider.get(PortNames::v_out)];

    float i = (v_nominal + v_gnd - v_bus) * g;
    i = std::clamp(i, -1000.0f, 1000.0f);

    st.through[provider.get(PortNames::v_out)] += i;
    st.through[provider.get(PortNames::v_in)] -= i;

    st.conductance[provider.get(PortNames::v_out)] += g;
    st.conductance[provider.get(PortNames::v_in)] += g;
}

template <typename Provider>
void Generator<Provider>::pre_load() {
    inv_internal_r = (internal_r > 0.0f) ? 1.0f / internal_r : 0.0f;
}

// =============================================================================
// GS24
// =============================================================================

template <typename Provider>
void GS24<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
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
void GS24<Provider>::post_step(SimulationState& st, float dt) {
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
void Transformer<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
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
void Inverter<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
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
void LerpNode<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v_input = st.across[provider.get(PortNames::input)];
    float v_output = st.across[provider.get(PortNames::output)];

    float g = 1.0f;
    st.conductance[provider.get(PortNames::output)] += g;

    float i = (v_input - v_output) * factor * g;
    st.through[provider.get(PortNames::output)] += i;
}

template <typename Provider>
void LerpNode<Provider>::post_step(SimulationState& st, float dt) {
    (void)dt;
    float v_input = st.across[provider.get(PortNames::input)];

    // 1. Branchless cold start
    current_value += (v_input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    // 2. Compute difference with deadzone
    float diff = v_input - current_value;
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    // 3. Apply interpolation with deadzone
    float new_output = current_value + factor * diff * dz_mask;
    current_value = new_output;
    st.across[provider.get(PortNames::output)] = new_output;
}

// =============================================================================
// PID
// =============================================================================

template <typename Provider>
void PID<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // High-impedance output: the PID drives the output directly in post_step
    // Small conductance keeps the node well-conditioned in the MNA matrix
    st.conductance[provider.get(PortNames::output)] += 1e-6f;
}

template <typename Provider>
void PID<Provider>::post_step(SimulationState& st, float dt) {
    float setpoint = st.across[provider.get(PortNames::setpoint)];
    float feedback = st.across[provider.get(PortNames::feedback)];

    // Self-contained dt clamping for testability (core also clamps, defense in depth)
    float safe_dt = std::max(1e-6f, std::min(dt, 0.1f));
    float inv_dt = 1.0f / safe_dt;

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
void PD<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // High-impedance output: the PD drives the output directly in post_step
    // Small conductance keeps the node well-conditioned in the MNA matrix
    st.conductance[provider.get(PortNames::output)] += 1e-6f;
}

template <typename Provider>
void PD<Provider>::post_step(SimulationState& st, float dt) {
    float setpoint = st.across[provider.get(PortNames::setpoint)];
    float feedback = st.across[provider.get(PortNames::feedback)];

    // Self-contained dt clamping for testability (core also clamps, defense in depth)
    float safe_dt = std::max(1e-6f, std::min(dt, 0.1f));
    float inv_dt = 1.0f / safe_dt;

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
void PI<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // High-impedance output: the PI drives the output directly in post_step
    // Small conductance keeps the node well-conditioned in the MNA matrix
    st.conductance[provider.get(PortNames::output)] += 1e-6f;
}

template <typename Provider>
void PI<Provider>::post_step(SimulationState& st, float dt) {
    float setpoint = st.across[provider.get(PortNames::setpoint)];
    float feedback = st.across[provider.get(PortNames::feedback)];

    // Self-contained dt clamping for testability (core also clamps, defense in depth)
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
void P<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // High-impedance output: the P controller drives the output directly in post_step
    // Small conductance keeps the node well-conditioned in the MNA matrix
    st.conductance[provider.get(PortNames::output)] += 1e-6f;
}

template <typename Provider>
void P<Provider>::post_step(SimulationState& st, float /*dt*/) {
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
void Splitter<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Splitter<Provider>::solve_mechanical(SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Splitter<Provider>::solve_hydraulic(SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Splitter<Provider>::solve_thermal(SimulationState& st, float /*dt*/) {}

// =============================================================================
// Merger (inverse of Splitter — 2-to-1, pure alias, no stamping)
// =============================================================================

template <typename Provider>
void Merger<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Merger<Provider>::solve_mechanical(SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Merger<Provider>::solve_hydraulic(SimulationState& st, float /*dt*/) {}

template <typename Provider>
void Merger<Provider>::solve_thermal(SimulationState& st, float /*dt*/) {}

// =============================================================================
// IndicatorLight
// =============================================================================

template <typename Provider>
void IndicatorLight<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
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
void HighPowerLoad<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::v_in)];
    float v_out = st.across[provider.get(PortNames::v_out)];
    float v_diff = v_in - v_out;

    // Branchless: use max to avoid negative division, conductance mask for threshold
    float safe_v_diff = std::max(v_diff, min_voltage_diff);
    float conduct_mask = (v_diff > min_voltage_diff) ? 1.0f : 0.0f;

    float i = power_draw / safe_v_diff * conduct_mask;
    float g = i / safe_v_diff;

    st.through[provider.get(PortNames::v_out)] += i;
    st.through[provider.get(PortNames::v_in)] -= i;
    st.conductance[provider.get(PortNames::v_out)] += g;
    st.conductance[provider.get(PortNames::v_in)] += g;
}

// =============================================================================
// Voltmeter
// =============================================================================

template <typename Provider>
void Voltmeter<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Voltmeter is purely visual - doesn't affect the circuit
}

// =============================================================================
// Gyroscope & AGK47
// =============================================================================

template <typename Provider>
void Gyroscope<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v_input = st.across[provider.get(PortNames::input)];
    st.conductance[provider.get(PortNames::input)] += conductance;
    st.through[provider.get(PortNames::input)] -= v_input * conductance;
}

template <typename Provider>
void AGK47<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v_input = st.across[provider.get(PortNames::input)];
    st.conductance[provider.get(PortNames::input)] += conductance;
    st.through[provider.get(PortNames::input)] -= v_input * conductance;
}

// =============================================================================
// ElectricPump
// =============================================================================

template <typename Provider>
void ElectricPump<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::v_in)];
    float g = 0.01f;
    st.conductance[provider.get(PortNames::v_in)] += g;
    st.through[provider.get(PortNames::v_in)] -= v_in * g;
}

template <typename Provider>
void ElectricPump<Provider>::solve_hydraulic(SimulationState& st, float /*dt*/) {
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
void SolenoidValve<Provider>::solve_hydraulic(SimulationState& st, float /*dt*/) {
    float ctrl = st.across[provider.get(PortNames::ctrl)];

    // Branchless: compute open state from control and normally_closed
    float ctrl_above = (ctrl > 12.0f) ? 1.0f : 0.0f;
    float nc_mask = normally_closed ? 1.0f : 0.0f;
    open_mask = std::abs(ctrl_above - nc_mask); // XOR: open when different

    float g = 1.0e6f * open_mask;
    st.conductance[provider.get(PortNames::flow_in)] += g;
    st.conductance[provider.get(PortNames::flow_out)] += g;
}

// =============================================================================
// InertiaNode
// =============================================================================

template <typename Provider>
void InertiaNode<Provider>::solve_mechanical(SimulationState& st, float /*dt*/) {
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
// Spring
// =============================================================================

template <typename Provider>
void Spring<Provider>::solve_mechanical(SimulationState& st, float /*dt*/) {
    float pA = st.across[provider.get(PortNames::pos_a)];
    float pB = st.across[provider.get(PortNames::pos_b)];

    // 1. Calculate current deformation
    float delta_x = (pA - pB) - rest_length;

    // 2. Spring force (Hooke's Law)
    float force = delta_x * k;

    // 3. If spring works only in compression (like in RUG-82 governor),
    //    cut off stretching forces (branchless select)
    float compression_mask = (compression_only) ? ((delta_x < 0.0f) ? 1.0f : 0.0f) : 1.0f;

    // Result (invert sign because spring resists compression)
    // std::abs ensures force is positive for compression
    st.across[provider.get(PortNames::force_out)] = std::abs(force) * compression_mask;
}

// =============================================================================
// TempSensor
// =============================================================================

template <typename Provider>
void TempSensor<Provider>::solve_thermal(SimulationState& st, float /*dt*/) {
    float temp_in = st.across[provider.get(PortNames::temp_in)];
    st.across[provider.get(PortNames::temp_out)] = temp_in * sensitivity;
}

// =============================================================================
// ElectricHeater
// =============================================================================

template <typename Provider>
void ElectricHeater<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::power)];
    float g = max_power / (v_in * v_in + 0.01f);
    st.conductance[provider.get(PortNames::power)] += g;
    st.through[provider.get(PortNames::power)] -= v_in * g * efficiency;
}

template <typename Provider>
void ElectricHeater<Provider>::solve_thermal(SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::power)];
    float heat_power = v_in * v_in * efficiency;
    st.through[provider.get(PortNames::heat_out)] += heat_power;
}

// =============================================================================
// RUG82
// =============================================================================

template <typename Provider>
void RUG82<Provider>::solve_electrical(SimulationState& st, float dt) {
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
void DMR400<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
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
void DMR400<Provider>::post_step(SimulationState& st, float dt) {
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
void RU19A<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
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
void RU19A<Provider>::solve_mechanical(SimulationState& st, float dt) {
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
void RU19A<Provider>::solve_thermal(SimulationState& st, float dt) {
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
void RU19A<Provider>::post_step(SimulationState& st, float dt) {
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
void Radiator<Provider>::solve_thermal(SimulationState& st, float /*dt*/) {
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
// AZS (Circuit Breaker)
// =============================================================================

template <typename Provider>
void AZS<Provider>::pre_load() {
    if (i_nominal > 0.0f) {
        r_heat = 1.0f / (i_nominal * i_nominal);
    }
}

template <typename Provider>
void AZS<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    v_out_old = st.across[provider.get(PortNames::v_out)];
    if (closed && downstream_g > 0.0f) {
        st.conductance[provider.get(PortNames::v_in)] += downstream_g;
        st.through[provider.get(PortNames::v_in)] += downstream_I - st.across[provider.get(PortNames::v_in)] * downstream_g;
    }
}

template <typename Provider>
void AZS<Provider>::solve_thermal(SimulationState& st, float dt) {
    // Use current computed in post_step (post-SOR, before v_out merge)
    float I = current;
    // T += (I² * r_heat - T * k_cool) * dt — vectorizable, no sqrt
    temp += (I * I * r_heat - temp * k_cool) * dt;
    if (temp < 0.0f) temp = 0.0f;
}

template <typename Provider>
void AZS<Provider>::post_step(SimulationState& st, float /*dt*/) {
    // 1. Manual toggle via control edge detection (same pattern as Switch)
    float current_control = st.across[provider.get(PortNames::control)];
    if (std::abs(current_control - last_control) > 0.1f) {
        if (!closed) tripped = false; // OFF→ON: clear tripped flag
        closed = !closed;
    }
    last_control = current_control;

    // 2. Thermal trip: if temp > 1.0, force open
    if (closed && temp > 1.0f) {
        closed = false;
        tripped = true;
    }

    // 3. Compute current through AZS (post-SOR, BEFORE voltage merge)
    // Since solve_electrical stamps downstream_g on v_in, SOR drives v_in ≈ v_out.
    // The actual current = what downstream load draws = conductance * voltage at v_out.
    if (closed) {
        float v_out = st.across[provider.get(PortNames::v_out)];
        float g_out = st.conductance[provider.get(PortNames::v_out)];
        current = g_out * v_out;
    } else {
        current = 0.0f;
    }

    // 4. Voltage merge (same pattern as Relay)
    if (closed) {
        downstream_g = st.conductance[provider.get(PortNames::v_out)];
        downstream_I = st.through[provider.get(PortNames::v_out)] + v_out_old * st.conductance[provider.get(PortNames::v_out)];
        st.across[provider.get(PortNames::v_out)] = st.across[provider.get(PortNames::v_in)];
    } else {
        downstream_g = 0.0f;
        downstream_I = 0.0f;
        current = 0.0f;
        st.across[provider.get(PortNames::v_out)] = 0.0f;
    }

    // 5. Output ports
    st.across[provider.get(PortNames::state)] = closed ? 1.0f : 0.0f;
    st.across[provider.get(PortNames::temp)] = temp;
    st.across[provider.get(PortNames::tripped)] = tripped ? 1.0f : 0.0f;
}

// =============================================================================
// Comparator
// =============================================================================

template <typename Provider>
void Comparator<Provider>::pre_load() {
    // Parameters are set by factory from JSON params
}

template <typename Provider>
void Comparator<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float Va = st.across[provider.get(PortNames::Va)];
    float Vb = st.across[provider.get(PortNames::Vb)];

    float diff = Va - Vb;

    bool set = (diff >= Von);
    bool keep = (diff > Voff);
    output_state = set || (output_state && keep);

    st.across[provider.get(PortNames::o)] = output_state ? 1.0f : 0.0f;
}

template <typename Provider>
void Subtract<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    st.across[provider.get(PortNames::o)] = A - B;
}

template <typename Provider>
void Multiply<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    st.across[provider.get(PortNames::o)] = A * B;
}

template <typename Provider>
void Divide<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    // Guard against division by zero
    st.across[provider.get(PortNames::o)] = (B != 0.0f) ? (A / B) : 0.0f;
}

template <typename Provider>
void Add<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    st.across[provider.get(PortNames::o)] = A + B;
}

// =============================================================================
// Logic Gates
// =============================================================================

template <typename Provider>
void AND<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    // Treat > 0.5V as TRUE, else FALSE
    bool a = (A > 0.5f);
    bool b = (B > 0.5f);
    bool result = a && b;
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void OR<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    bool a = (A > 0.5f);
    bool b = (B > 0.5f);
    bool result = a || b;
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void XOR<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    bool a = (A > 0.5f);
    bool b = (B > 0.5f);
    bool result = a != b;
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void NOT<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    bool a = (A > 0.5f);
    bool result = !a;
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void NAND<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    bool a = (A > 0.5f);
    bool b = (B > 0.5f);
    bool result = !(a && b);
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void Any_V_to_Bool<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float vin = st.across[provider.get(PortNames::Vin)];
    // Convert any non-zero voltage to TRUE (including negative) using bit trick
    uint32_t b;
    std::memcpy(&b, &vin, sizeof(b));
    bool result = (b + b) != 0;
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

template <typename Provider>
void Positive_V_to_Bool<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float vin = st.across[provider.get(PortNames::Vin)];
    // Convert positive voltage to TRUE (v > 0)
    bool result = vin > 0.0f;
    st.across[provider.get(PortNames::o)] = result ? 1.0f : 0.0f;
}

// =============================================================================
// LUT - Lookup table with linear interpolation (arena-based)
// =============================================================================

template <typename Provider>
float LUT<Provider>::interpolate(float x, const float* keys, const float* vals, uint16_t size) {
    if (size == 0) return 0.0f;
    if (size == 1) return vals[0];

    // Branchless linear scan: accumulate the index of the last key <= x.
    // For typical LUT sizes (5-30 entries) this is faster than binary search
    // because it avoids branch mispredictions and is auto-vectorizable.
    uint16_t lo = 0;
    for (uint16_t i = 1; i < size; ++i) {
        lo += (keys[i] <= x);  // branchless: 0 or 1
    }

    // Clamp to valid interval [0, size-2]
    uint16_t hi = lo + 1;
    if (hi >= size) { lo = size - 2; hi = size - 1; }

    // Branchless lerp: when x <= keys[0], lo==0 and t<=0 → clamps to vals[0]
    float denom = keys[hi] - keys[lo];
    float t = (denom > 0.0f) ? (x - keys[lo]) / denom : 0.0f;
    // Clamp t to [0,1] for edge cases (x < keys[0] or x > keys[size-1])
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return vals[lo] + t * (vals[hi] - vals[lo]);
}

template <typename Provider>
void LUT<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float x = st.across[provider.get(PortNames::input)];
    const float* keys = st.lut_keys.data() + table_offset;
    const float* vals = st.lut_values.data() + table_offset;
    st.across[provider.get(PortNames::output)] = interpolate(x, keys, vals, table_size);
}

template <typename Provider>
bool LUT<Provider>::parse_table(const std::string& table_str,
                                std::vector<float>& keys,
                                std::vector<float>& values) {
    keys.clear();
    values.clear();
    if (table_str.empty()) return false;

    size_t pos = 0;
    while (pos < table_str.size()) {
        // Skip whitespace and semicolons
        while (pos < table_str.size() && (table_str[pos] == ' ' || table_str[pos] == ';'))
            ++pos;
        if (pos >= table_str.size()) break;

        // Find colon separator
        size_t colon = table_str.find(':', pos);
        if (colon == std::string::npos) break;

        // Find end of value (next semicolon or end)
        size_t end = table_str.find(';', colon + 1);
        if (end == std::string::npos) end = table_str.size();

        try {
            float k = std::stof(table_str.substr(pos, colon - pos));
            float v = std::stof(table_str.substr(colon + 1, end - colon - 1));
            keys.push_back(k);
            values.push_back(v);
        } catch (...) {
            break;
        }
        pos = end;
    }
    return !keys.empty();
}

// =============================================================================
// FastTMO
// =============================================================================

template <typename Provider>
void FastTMO<Provider>::pre_load() {
    inv_tau = 1.0f / std::max(tau, 0.0001f);
}

template <typename Provider>
void FastTMO<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.across[in_idx];

    // 1. Branchless Cold Start
    current_value += (input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    // 2. Branchless TMO Logic
    float diff = input - current_value;
    float factor = std::min(dt * inv_tau, 1.0f);
    // f32.select equivalent
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    current_value += diff * factor * dz_mask;
    st.across[out_idx] = current_value;
}

// =============================================================================
// AsymTMO
// =============================================================================

template <typename Provider>
void AsymTMO<Provider>::pre_load() {
    inv_tau_up = 1.0f / std::max(tau_up, 0.0001f);
    inv_tau_down = 1.0f / std::max(tau_down, 0.0001f);
}

template <typename Provider>
void AsymTMO<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.across[in_idx];

    // 1. Branchless Cold Start
    current_value += (input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    // 2. Branchless Asymmetric Logic
    float diff = input - current_value;
    // WASM f32.select for tau selection
    float active_inv_tau = (diff > 0.0f) ? inv_tau_up : inv_tau_down;

    float factor = std::min(dt * active_inv_tau, 1.0f);
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    current_value += diff * factor * dz_mask;
    st.across[out_idx] = current_value;
}

// =============================================================================
// SlewRate
// =============================================================================

template <typename Provider>
void SlewRate<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.across[in_idx];

    // 1. Instant initialization on first frame (branchless)
    current_value += (input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    // 2. Compute desired change
    float diff = input - current_value;

    // 3. Compute limit per step for current dt
    float max_step = max_rate * dt;

    // 4. Clamp differential (WASM friendly clamp)
    float limited_diff = std::max(-max_step, std::min(max_step, diff));

    // 5. Apply deadzone mask to avoid "dithering" around target
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    current_value += limited_diff * dz_mask;
    st.across[out_idx] = current_value;
}

// =============================================================================
// AsymSlewRate
// =============================================================================

template <typename Provider>
void AsymSlewRate<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.across[in_idx];

    // 1. Branchless Cold Start
    current_value += (input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    float diff = input - current_value;

    // 2. Select active rate (WASM f32.select)
    // If rising - rate_up, if falling - rate_down
    float active_rate = (diff > 0.0f) ? rate_up : rate_down;

    // 3. Limit step for current frame
    float max_step = active_rate * dt;

    // 4. Branchless Clamp & Deadzone
    // Limit increment to [-max_step, max_step]
    float limited_diff = std::max(-max_step, std::min(max_step, diff));
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    current_value += limited_diff * dz_mask;
    st.across[out_idx] = current_value;
}

// =============================================================================
// TimeDelay
// =============================================================================

template <typename Provider>
void TimeDelay<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);

    // Convert input to 0.0 or 1.0
    float raw_in = (st.across[in_idx] > 0.5f) ? 1.0f : 0.0f;

    // 1. Cold start (branchless)
    current_out += (raw_in - current_out) * first_frame_mask;
    last_in += (raw_in - last_in) * first_frame_mask;  // Sync on cold start only
    first_frame_mask = 0.0f;

    // 2. Reset logic: if input changed from last frame, zero the timer
    // WASM f32.select: keep accumulator if raw_in == last_in, else 0
    accumulator = (raw_in == last_in) ? (accumulator + dt) : 0.0f;
    last_in = raw_in;

    // 3. Select time threshold (delay_on if targeting 1, delay_off if targeting 0)
    float target_delay = (raw_in > 0.5f) ? delay_on : delay_off;

    // 4. Check timer expiration and state difference
    bool timer_expired = (accumulator >= target_delay);
    bool state_differs = (raw_in != current_out);

    // Update output only if timer expired
    current_out = (timer_expired && state_differs) ? raw_in : current_out;

    st.across[out_idx] = current_out;
}

// =============================================================================
// Monostable
// =============================================================================

template <typename Provider>
void Monostable<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);

    // Convert input to 0.0 or 1.0
    float raw_in = (st.across[in_idx] > 0.5f) ? 1.0f : 0.0f;

    // Rising edge detector
    bool trigger = (raw_in > 0.5f && last_in <= 0.5f);
    last_in = raw_in;

    // If triggered, reset timer to duration, otherwise tick down to 0
    timer = trigger ? duration : std::max(0.0f, timer - dt);

    // Output is active while timer > 0
    st.across[out_idx] = (timer > 0.0f) ? 1.0f : 0.0f;
}

// =============================================================================
// SampleHold
// =============================================================================

template <typename Provider>
void SampleHold<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t trig_idx = provider.get(PortNames::trigger);
    uint32_t out_idx = provider.get(PortNames::out);

    float val_in = st.across[in_idx];
    float trig_in = st.across[trig_idx];

    // Rising edge detector
    bool is_rising = (trig_in > 0.5f && last_trig <= 0.5f);
    last_trig = trig_in;

    // If rising edge, update stored value, otherwise keep old value
    stored_value = is_rising ? val_in : stored_value;

    st.across[out_idx] = stored_value;
}

// =============================================================================
// Integrator
// =============================================================================

template <typename Provider>
void Integrator<Provider>::solve_logical(SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t reset_idx = provider.get(PortNames::reset);
    uint32_t out_idx = provider.get(PortNames::out);

    float val_in = st.across[in_idx];
    float reset_in = st.across[reset_idx];

    // 1. Cold Start
    accumulator += (initial_val - accumulator) * first_frame_mask;
    first_frame_mask = 0.0f;

    // 2. Integration: accumulate with gain scaling
    accumulator += val_in * gain * dt;

    // 3. Reset: if reset signal > 0.5, zero out (branchless)
    accumulator = (reset_in > 0.5f) ? 0.0f : accumulator;

    st.across[out_idx] = accumulator;
}

// =============================================================================
// Clamp
// =============================================================================

template <typename Provider>
void Clamp<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);

    float input = st.across[in_idx];

    // std::clamp compiles to f32.min/f32.max in WASM
    st.across[out_idx] = std::clamp(input, min, max);
}

// =============================================================================
// Normalize
// =============================================================================

template <typename Provider>
void Normalize<Provider>::pre_load() {
    // Предрасчитываем инверсный диапазон, чтобы избежать деления в solve
    float range = max - min;
    inv_range = (std::abs(range) > 1e-6f) ? (1.0f / range) : 0.0f;
}

template <typename Provider>
void Normalize<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);

    float input = st.across[in_idx];

    // Линейное преобразование: (x - min) * (1 / range)
    float normalized = (input - min) * inv_range;

    // Всегда ограничиваем результат в 0..1 для безопасности последующей логики
    st.across[out_idx] = std::clamp(normalized, 0.0f, 1.0f);
}

// =============================================================================
// Min / Max
// =============================================================================

template <typename Provider>
void Min<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    st.across[provider.get(PortNames::o)] = std::min(A, B);
}

template <typename Provider>
void Max<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    st.across[provider.get(PortNames::o)] = std::max(A, B);
}

// =============================================================================
// Comparison Operators (Greater / Lesser / GreaterEq / LesserEq)
// =============================================================================

template <typename Provider>
void Greater<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    // Branchless: результат сравнения приводится к float (1.0 или 0.0)
    st.across[provider.get(PortNames::o)] = (A > B) ? 1.0f : 0.0f;
}

template <typename Provider>
void Lesser<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    st.across[provider.get(PortNames::o)] = (A < B) ? 1.0f : 0.0f;
}

template <typename Provider>
void GreaterEq<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    st.across[provider.get(PortNames::o)] = (A >= B) ? 1.0f : 0.0f;
}

template <typename Provider>
void LesserEq<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    st.across[provider.get(PortNames::o)] = (A <= B) ? 1.0f : 0.0f;
}

// =============================================================================
// Explicit Template Instantiation for JitProvider
// =============================================================================

#include "explicit_instantiations.h"
