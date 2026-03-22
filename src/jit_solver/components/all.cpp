#include "components/all.h"
#include "components/port_registry.h"
#include "../state.h"
#include "../parse_number.h"
#include <cmath>
#include <cstring>

// =============================================================================
// Battery
// =============================================================================

template <typename Provider>
void Battery<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float g = inv_internal_r;

    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::v_out), provider.get(PortNames::v_in), g);
    st.through[provider.get(PortNames::v_out)] += v_nominal * g;
    st.through[provider.get(PortNames::v_in)] -= v_nominal * g;
}

template <typename Provider>
void Battery<Provider>::pre_load() {
    float safe_r = std::max(internal_r, 1e-6f);
    inv_internal_r = 1.0f / safe_r;
}

// =============================================================================
// Switch
// =============================================================================

template <typename Provider>
void Switch<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    v_out_old = st.across[provider.get(PortNames::v_out)];
    // Branchless: mask is 0 when open or downstream_g <= 0
    float mask = (closed && downstream_g > 0.0f) ? 1.0f : 0.0f;
    float g = downstream_g * mask;
    float i = downstream_I * mask;
    st.conductance[provider.get(PortNames::v_in)] += g;
    st.through[provider.get(PortNames::v_in)] += i - st.across[provider.get(PortNames::v_in)] * g;
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
        downstream_I = st.through[provider.get(PortNames::v_out)] + v_out_old * st.conductance[provider.get(PortNames::v_out)];
        st.across[provider.get(PortNames::v_out)] = st.across[provider.get(PortNames::v_in)];
    } else {
        downstream_g = 0.0f;
        downstream_I = 0.0f;
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
    // Branchless: mask is 0 when open or downstream_g <= 0
    float mask = (closed && downstream_g > 0.0f) ? 1.0f : 0.0f;
    float g = downstream_g * mask;
    float i = downstream_I * mask;
    st.conductance[provider.get(PortNames::v_in)] += g;
    st.through[provider.get(PortNames::v_in)] += i - st.across[provider.get(PortNames::v_in)] * g;
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
    // Branchless: mask is 0 when not pressed or downstream_g <= 0
    float mask = (is_pressed && downstream_g > 0.0f) ? 1.0f : 0.0f;
    float g = downstream_g * mask;
    float i = downstream_I * mask;
    st.conductance[provider.get(PortNames::v_in)] += g;
    st.through[provider.get(PortNames::v_in)] += i - st.across[provider.get(PortNames::v_in)] * g;
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
    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::input), conductance);
}

// =============================================================================
// RefNode
// =============================================================================

template <typename Provider>
void RefNode<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // [BUG-RefNode] Fixed: use stamp_voltage_source for correct Norton residual.
    // Previously stamped through += value * g without subtracting across[v] * g,
    // causing SOR divergence for any value != 0 (voltage grew unbounded each iteration).
    stamp_voltage_source(st.conductance.data(), st.through.data(), st.across.data(),
                         provider.get(PortNames::v), value, 1.0e-6f);
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

    // Norton equivalent: same stamp as Battery
    // stamp_two_port handles conductance and residual current between two nodes
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::v_out), provider.get(PortNames::v_in), g);

    // Voltage source contribution: I_source = V_nominal * G
    st.through[provider.get(PortNames::v_out)] += v_nominal * g;
    st.through[provider.get(PortNames::v_in)] -= v_nominal * g;
}

template <typename Provider>
void Generator<Provider>::pre_load() {
    // Match Battery's safety pattern: floor resistance instead of zeroing out
    float safe_r = std::max(internal_r, 1e-6f);
    inv_internal_r = 1.0f / safe_r;
}

// =============================================================================
// GS24
// =============================================================================

template <typename Provider>
void GS24<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float rpm_percent = current_rpm * inv_target_rpm;

    // === Starter mode contribution (branchless) ===
    float starter_mask = (mode == GS24Mode::STARTER) ? 1.0f : 0.0f;

    float g_starter = inv_r_internal;
    float back_emf = k_motor * current_rpm;
    float i_source_starter = std::clamp(back_emf * g_starter, 0.0f, i_max_starter);

    // === Generator mode contribution (branchless) ===
    float gen_mask = (mode == GS24Mode::GENERATOR) ? 1.0f : 0.0f;

    // Branchless phi ramp: clamp((rpm_percent - rpm_threshold) / 0.2, 0, 1)
    float phi = std::clamp((rpm_percent - rpm_threshold) * 5.0f, 0.0f, 1.0f);

    float k_mod_val = provider.has(PortNames::k_mod) ? st.across[provider.get(PortNames::k_mod)] : 1.0f;
    float i_no = std::clamp(i_max * phi * k_mod_val, 0.0f, 100.0f);
    float g_gen = inv_r_norton;

    // === Combine: each path stamps into the same port, scaled by its mask ===
    // Starter: one-port-to-ground load (draws current) + back-EMF source
    // Generator: one-port-to-ground source (drives voltage up)
    float g_total = g_starter * starter_mask + g_gen * gen_mask;
    float i_total = i_source_starter * starter_mask + i_no * gen_mask;

    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::v_out), g_total);
    st.through[provider.get(PortNames::v_out)] += i_total;
}

template <typename Provider>
void GS24<Provider>::pre_load() {
    inv_r_internal = 1.0f / std::max(r_internal, 1e-6f);
    inv_r_norton = 1.0f / std::max(r_norton, 1e-6f);
    inv_target_rpm = 1.0f / std::max(target_rpm, 1.0f);
}

template <typename Provider>
void GS24<Provider>::post_step(SimulationState& st, float dt) {
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

// =============================================================================
// Transformer
// =============================================================================

template <typename Provider>
void Transformer<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v_primary = st.across[provider.get(PortNames::primary)];
    float v_secondary = st.across[provider.get(PortNames::secondary)];
    float v_secondary_target = v_primary * ratio;

    // [BUG-Transformer] Fixed: use proper Norton residual (v_target - v_current) * g
    // instead of just v_target * g, which caused divergent SOR accumulation.
    // Secondary side: voltage source driving toward v_primary * ratio
    float g_secondary = 1.0f;
    float i_secondary = (v_secondary_target - v_secondary) * g_secondary;
    st.conductance[provider.get(PortNames::secondary)] += g_secondary;
    st.through[provider.get(PortNames::secondary)] += i_secondary;

    // [BUG-Transformer-Primary] Fixed: add reflected secondary voltage source.
    // Previously stamped only through -= v_primary * g_primary (pure drain to ground).
    // For power conservation, primary must see reflected secondary voltage:
    //   V_primary_reflected = V_secondary / ratio
    //   Norton residual: (V_secondary/ratio - V_primary) * g_primary
    float g_primary = g_secondary * ratio * ratio;
    float v_primary_reflected = v_secondary / std::max(std::abs(ratio), 1e-6f);
    st.conductance[provider.get(PortNames::primary)] += g_primary;
    st.through[provider.get(PortNames::primary)] += (v_primary_reflected - v_primary) * g_primary;
}

// =============================================================================
// Inverter
// =============================================================================

template <typename Provider>
void Inverter<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v_dc = st.across[provider.get(PortNames::dc_in)];
    float v_ac = st.across[provider.get(PortNames::ac_out)];
    float v_ac_target = v_dc * efficiency;

    // [BUG-Inverter] Fixed: use proper Norton residual (v_target - v_current) * g
    // instead of just v_target * g, which caused divergent SOR accumulation.
    float g = 1.0f;
    float i_ac = (v_ac_target - v_ac) * g;
    st.conductance[provider.get(PortNames::ac_out)] += g;
    st.through[provider.get(PortNames::ac_out)] += i_ac;

    // [BUG-Inverter-DC] Fixed: add reflected AC voltage source on DC input.
    // Previously stamped only through -= v_dc * g_dc (pure drain to ground).
    // For energy conservation, DC input must see reflected AC voltage:
    //   V_dc_reflected = V_ac / efficiency
    //   Norton residual: (V_dc_reflected - V_dc) * g_dc
    float g_dc = g * efficiency;
    float v_dc_reflected = v_ac / std::max(efficiency, 1e-6f);
    st.conductance[provider.get(PortNames::dc_in)] += g_dc;
    st.through[provider.get(PortNames::dc_in)] += (v_dc_reflected - v_dc) * g_dc;
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
// CurrentSense
// =============================================================================

template <typename Provider>
void CurrentSense<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float g = conductance;

    // High-conductance two-port: near-zero voltage drop between v_in and v_out
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::v_out), provider.get(PortNames::v_in), g);

    // Measured current: I = (v_in - v_out) * G
    float v_diff = st.across[provider.get(PortNames::v_in)] - st.across[provider.get(PortNames::v_out)];
    st.across[provider.get(PortNames::i_out)] = v_diff * g;
}

// =============================================================================
// IndicatorLight
// =============================================================================

template <typename Provider>
void IndicatorLight<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float g = conductance;

    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::v_out), provider.get(PortNames::v_in), g);

    float v_diff = st.across[provider.get(PortNames::v_in)] - st.across[provider.get(PortNames::v_out)];
    float normalized = std::clamp(v_diff * inv_rated_voltage, 0.0f, 1.0f);
    st.across[provider.get(PortNames::brightness)] = normalized * max_brightness;
}

template <typename Provider>
void IndicatorLight<Provider>::pre_load() {
    inv_rated_voltage = 1.0f / std::max(rated_voltage, 1e-6f);
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

    // Constant-power load: P = V * I → I = P / V, G = I / V = P / V²
    float g = power_draw / (safe_v_diff * safe_v_diff) * conduct_mask;

    // Use standard two-port stamp: current flows from v_in to v_out through the load
    // stamp_two_port handles SOR-compatible residual decomposition
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::v_in), provider.get(PortNames::v_out), g);
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
    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::input), conductance);
}

template <typename Provider>
void AGK47<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::input), conductance);
}

// =============================================================================
// ElectricPump
// =============================================================================

template <typename Provider>
void ElectricPump<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Load-dependent motor draw: I = P_hydraulic / V_bus
    // P_hydraulic ≈ (p_out - p_in) * flow, approximated as pressure_diff * g_coupling
    // For simplicity, use conductance proportional to output pressure differential
    // scaled by 1/V_bus to get a realistic current draw.
    float v_in = st.across[provider.get(PortNames::v_in)];
    float p_out = st.across[provider.get(PortNames::p_out)];
    float p_in_h = st.across[provider.get(PortNames::p_in)];
    float dp = std::max(p_out - p_in_h, 0.0f);

    // G_motor = k * dp / V_bus^2 (constant-power load model)
    // Floor at G_IDLE = 0.01 to keep the bus well-conditioned even with no load.
    constexpr float G_IDLE = 0.01f;
    constexpr float K_ELEC = 0.001f;  // Hydraulic-to-electrical coupling coefficient
    float g_load = K_ELEC * dp / (v_in * v_in + 1.0f);
    float g_total = G_IDLE + g_load;

    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::v_in), g_total);
}

template <typename Provider>
void ElectricPump<Provider>::solve_hydraulic(SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::v_in)];
    float p_in_h = st.across[provider.get(PortNames::p_in)];
    float p_out = st.across[provider.get(PortNames::p_out)];

    // Target pressure boost: proportional to bus voltage
    float target_p = v_in * max_pressure / 28.0f;

    // Two-port coupling: internal leakage path from p_in to p_out.
    // Small conductance allows return flow when pump is off; the boost source
    // dominates when the pump is powered, maintaining the pressure differential.
    float g_coupling = 0.1f;
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::p_in), provider.get(PortNames::p_out),
                   g_coupling);

    // Norton source on p_out: drives the output above input by target_p.
    // The effective target for p_out is (p_in + target_p).
    // Self-correcting residual: (p_in + target_p - p_out) * g
    // g_boost >> g_coupling so the pump maintains the pressure differential.
    float g_boost = 10.0f;
    float p_target_abs = p_in_h + target_p;
    st.conductance[provider.get(PortNames::p_out)] += g_boost;
    st.through[provider.get(PortNames::p_out)] += (p_target_abs - p_out) * g_boost;
}

// =============================================================================
// SolenoidValve
// =============================================================================

template <typename Provider>
void SolenoidValve<Provider>::solve_hydraulic(SimulationState& st, float /*dt*/) {
    float ctrl = st.across[provider.get(PortNames::ctrl)];

    // Branchless: compute open state from control and normally_closed
    // NC valve: opens when ctrl > 12V (ctrl_above=1, nc=1 → open)
    // NO valve: opens when ctrl <= 12V (ctrl_above=0, nc=0 → open)
    float ctrl_above = (ctrl > 12.0f) ? 1.0f : 0.0f;
    float no_mask = normally_closed ? 0.0f : 1.0f;
    open_mask = std::abs(ctrl_above - no_mask); // XOR with inverted sense

    // [BUG-SolenoidValve] Fixed: use stamp_two_port to couple flow_in and flow_out.
    // Previously stamped independent conductances on each port (two disconnected
    // loads to ground). A valve must pass fluid through - stamp_two_port creates
    // a conductance path between the two ports.
    // g = 100 when open: large relative to other hydraulic components (g=1..10)
    // for near-zero pressure drop, while remaining SOR-stable.
    float g = 100.0f * open_mask;
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::flow_in), provider.get(PortNames::flow_out),
                   g);
}

// =============================================================================
// GidroAccumulator
// =============================================================================

template <typename Provider>
void GidroAccumulator<Provider>::solve_hydraulic(SimulationState& st, float /*dt*/) {
    float p_in  = st.across[provider.get(PortNames::p_in)];
    float p_out = st.across[provider.get(PortNames::p_out)];

    // Boyle's law: P_precharge * V_total = P_gas * V_gas
    // Gas pressure at current gas_volume:
    float p_gas = precharge_pressure * volume / std::max(gas_volume, 0.01f);

    // Flow direction: fluid enters when system pressure > gas pressure
    // High conductance coupling between p_in and p_out when accumulator is active
    // (accumulator acts as a pressure buffer between input and output)
    float g_coupling = 10.0f;
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::p_in), provider.get(PortNames::p_out),
                   g_coupling);

    // Norton source on p_out: drive toward gas pressure (self-correcting)
    float g_gas = 1.0f;
    st.conductance[provider.get(PortNames::p_out)] += g_gas;
    st.through[provider.get(PortNames::p_out)] += (p_gas - p_out) * g_gas;
}

template <typename Provider>
void GidroAccumulator<Provider>::post_step(SimulationState& st, float dt) {
    // [BUG-GidroAccumulator] Fixed: moved gas_volume mutation from solve_hydraulic
    // to post_step. Mutating state inside the SOR iteration loop caused convergence
    // issues because gas_volume would change on every iteration, shifting the target
    // pressure during convergence.
    float p_in = st.across[provider.get(PortNames::p_in)];
    float p_gas = precharge_pressure * volume / std::max(gas_volume, 0.01f);

    // Update gas volume based on pressure differential
    // When p_in > p_gas, fluid enters (gas_volume decreases)
    // When p_in < p_gas, fluid exits (gas_volume increases)
    float dp = p_in - p_gas;
    float flow_rate = dp * 0.001f;  // Small flow coefficient
    gas_volume = std::clamp(gas_volume - flow_rate * dt, 0.1f, volume);
}

template <typename Provider>
void GidroAccumulator<Provider>::pre_load() {
    gas_volume = std::clamp(gas_volume, 0.1f, volume);
}

// =============================================================================
// FuelTank
// =============================================================================

template <typename Provider>
void FuelTank<Provider>::solve_hydraulic(SimulationState& st, float /*dt*/) {
    float flow_out_v = st.across[provider.get(PortNames::flow_out)];

    // Gravity head pressure: P = rho * g * h
    // Approximate h from fuel level fraction * tank_height (assume 1m tank)
    float level_frac = level * inv_capacity;
    float gravity_pressure = density * 9.81f * level_frac; // Pa (simplified)

    // Norton source on flow_out: drive toward gravity_pressure (self-correcting)
    float g = 1.0f;
    st.conductance[provider.get(PortNames::flow_out)] += g;
    st.through[provider.get(PortNames::flow_out)] += (gravity_pressure - flow_out_v) * g;

    // Output fuel level as a logical signal (0..1 fraction)
    st.across[provider.get(PortNames::level_out)] = level_frac;
}

template <typename Provider>
void FuelTank<Provider>::post_step(SimulationState& st, float dt) {
    // Consume fuel based on flow drawn from tank
    // flow = through[flow_out] represents flow rate out of the tank
    float flow = st.through[provider.get(PortNames::flow_out)];
    float consumption = std::max(flow, 0.0f) * dt;
    level = std::max(level - consumption, 0.0f);
}

template <typename Provider>
void FuelTank<Provider>::pre_load() {
    inv_capacity = 1.0f / std::max(capacity, 1e-6f);
    level = std::clamp(level, 0.0f, capacity);
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
    inv_mass = 1.0f / std::max(mass, 1e-6f);
}

// =============================================================================
// Spring
// =============================================================================

template <typename Provider>
void Spring<Provider>::solve_mechanical(SimulationState& st, float dt) {
    float pA = st.across[provider.get(PortNames::pos_a)];
    float pB = st.across[provider.get(PortNames::pos_b)];

    // 1. Calculate current deformation
    float delta_x = (pA - pB) - rest_length;

    // 2. Branchless cold start: initialize prev_delta_x on first frame
    prev_delta_x += (delta_x - prev_delta_x) * first_frame_mask;
    first_frame_mask = 0.0f;

    // 3. Spring force (Hooke's Law): F_spring = k * |delta_x|
    float spring_force = delta_x * k;

    // 4. Viscous damping force: F_damp = c * velocity
    //    velocity ≈ (delta_x - prev_delta_x) / dt (finite difference)
    //    One division per mechanical step (20 Hz) — acceptable
    float inv_dt = 1.0f / std::max(dt, 1e-6f);
    float velocity = (delta_x - prev_delta_x) * inv_dt;
    float damping_force = c * velocity;

    // 5. Total force = spring + damping (both resist motion)
    float total_force = spring_force + damping_force;

    // 6. If spring works only in compression (like in RUG-82 governor),
    //    cut off stretching forces (branchless select)
    //    co_f: 1.0 means "compression only mode", 0.0 means "both directions"
    //    When co_f == 1.0: mask = (delta_x < 0) ? 1 : 0
    //    When co_f == 0.0: mask = 1.0 (always active)
    float co_f = static_cast<float>(compression_only); // branchless bool→float (0.0 or 1.0)
    float comp_mask = (delta_x < 0.0f) ? 1.0f : 0.0f;
    float compression_mask = comp_mask * co_f + (1.0f - co_f);

    // 7. Result: std::abs ensures force magnitude is always non-negative
    st.across[provider.get(PortNames::force_out)] = std::abs(total_force) * compression_mask;

    // 8. Store for next frame
    prev_delta_x = delta_x;
}

template <typename Provider>
void Spring<Provider>::pre_load() {
    compression_only_f = compression_only ? 1.0f : 0.0f;
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
    // Heater is a nonlinear resistive load: P = max_power at rated voltage.
    // Conductance: G = P / V² (with safety floor to avoid div-by-zero)
    float v_in = st.across[provider.get(PortNames::power)];
    float g = max_power / (v_in * v_in + 0.01f);

    // Full electrical power is dissipated (efficiency applies only to thermal output)
    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::power), g);
}

template <typename Provider>
void ElectricHeater<Provider>::solve_thermal(SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::power)];
    // [BUG-23] Fixed: thermal output must match actual electrical power dissipated.
    // Electrical conductance: g = max_power / (v² + 0.01), so P_elec = v² * g.
    // Old code used v² * efficiency, which is unbounded (e.g., 28² * 0.9 = 705W
    // regardless of max_power setting).
    float v_sq = v_in * v_in;
    float g = max_power / (v_sq + 0.01f);
    float heat_power = v_sq * g * efficiency;
    st.through[provider.get(PortNames::heat_out)] += heat_power;
}

// =============================================================================
// RUG82
// =============================================================================

template <typename Provider>
void RUG82<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // [BUG-RUG82] Fixed: moved integration to post_step.
    // Previously, k_mod += kp * error * dt ran inside solve_electrical, which
    // executes on every SOR iteration. This caused N-times-higher effective gain
    // (where N = number of SOR iterations), making regulator behavior non-deterministic.
    // Now solve_electrical only writes the current k_mod output.
    st.across[provider.get(PortNames::k_mod)] = k_mod;
}

template <typename Provider>
void RUG82<Provider>::post_step(SimulationState& st, float dt) {
    // Integration runs once per frame (after solver converges)
    float v_gen = st.across[provider.get(PortNames::v_gen)];
    float error = v_target - v_gen;
    k_mod += kp * error * dt;
    k_mod = std::clamp(k_mod, 0.0f, 1.0f);
    st.across[provider.get(PortNames::k_mod)] = k_mod;
}

// =============================================================================
// DMR400
// =============================================================================

template <typename Provider>
void DMR400<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // When closed, the DMR acts as a low-impedance connection between
    // generator reference and output bus. Model as a high-conductance two-port.
    // Branchless: multiply G by closed_mask (0.0 or 1.0)
    constexpr float G_CLOSED = 100.0f;
    float closed_mask = is_closed ? 1.0f : 0.0f;
    float g = G_CLOSED * closed_mask;
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::v_gen_ref), provider.get(PortNames::v_out), g);
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
    float rpm_percent = current_rpm * inv_target_rpm;

    // === Starter contribution (CRANKING or IGNITION) ===
    float starter_mask = (this->state == APUState::CRANKING || this->state == APUState::IGNITION) ? 1.0f : 0.0f;

    constexpr float R_START_INTERNAL = 0.025f;
    constexpr float G_START = 1.0f / R_START_INTERNAL;  // 40.0 — compile-time constant
    constexpr float K_MOTOR_BACK_EMF = 38.0f;
    constexpr float I_MAX_START = 1000.0f;

    float back_emf = K_MOTOR_BACK_EMF * rpm_percent;
    float i_source_starter = std::clamp(back_emf * G_START, 0.0f, I_MAX_START);

    // === Generator contribution (RUNNING) ===
    float gen_mask = (this->state == APUState::RUNNING) ? 1.0f : 0.0f;

    // Branchless phi ramp: clamp((rpm_percent - 0.4) / 0.2, 0, 1)
    float phi = std::clamp((rpm_percent - 0.4f) * 5.0f, 0.0f, 1.0f);

    float k_mod = st.across[provider.get(PortNames::k_mod)];
    float i_no = std::clamp(400.0f * phi * k_mod, 0.0f, 100.0f);

    constexpr float G_NORTON = 1.0f / 0.08f;  // 12.5 — compile-time constant

    // === Combine masked contributions ===
    float g_total = G_START * starter_mask + G_NORTON * gen_mask;
    float i_total = i_source_starter * starter_mask + i_no * gen_mask;

    // Both paths stamp on different ports — use select
    // Starter uses v_start, generator uses v_bus
    // Stamp starter port (zero contribution when starter_mask == 0)
    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::v_start), G_START * starter_mask);
    st.through[provider.get(PortNames::v_start)] += i_source_starter * starter_mask;

    // Stamp generator port (zero contribution when gen_mask == 0)
    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::v_bus), G_NORTON * gen_mask);
    st.through[provider.get(PortNames::v_bus)] += i_no * gen_mask;
}

template <typename Provider>
void RU19A<Provider>::solve_mechanical(SimulationState& st, float dt) {
    float v_bus = st.across[provider.get(PortNames::v_bus)];

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

    // rpm_out canonical value is set by post_step (percentage 0-100).
    // No write here to avoid mid-step inconsistency.
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

    float rpm_percent = current_rpm * inv_target_rpm;
    st.across[provider.get(PortNames::rpm_out)] = rpm_percent * 100.0f;
    st.across[provider.get(PortNames::t4_out)] = t4;
}

// RU19A pre_load: precompute inverse target RPM
template <typename Provider>
void RU19A<Provider>::pre_load() {
    inv_target_rpm = 1.0f / std::max(target_rpm, 1.0f);
}

// =============================================================================
// Radiator
// =============================================================================

template <typename Provider>
void Radiator<Provider>::solve_thermal(SimulationState& st, float /*dt*/) {
    // [BUG-Radiator] Fixed: was using inverted sign convention.
    // stamp_two_port correctly computes i = (V2 - V1) * g, injecting current
    // to pull both nodes toward each other (heat transfer from hot to cold).
    float g = cooling_capacity;
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::heat_in), provider.get(PortNames::heat_out), g);
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
    // Branchless: mask is 0 when open or downstream_g <= 0
    float mask = (closed && downstream_g > 0.0f) ? 1.0f : 0.0f;
    float g = downstream_g * mask;
    float i = downstream_I * mask;
    st.conductance[provider.get(PortNames::v_in)] += g;
    st.through[provider.get(PortNames::v_in)] += i - st.across[provider.get(PortNames::v_in)] * g;
}

template <typename Provider>
void AZS<Provider>::solve_thermal(SimulationState& st, float dt) {
    // Use current computed in post_step (post-SOR, before v_out merge)
    float I = current;
    // T += (I² * r_heat - T * k_cool) * dt — vectorizable, no sqrt
    temp += (I * I * r_heat - temp * k_cool) * dt;
    // Branchless floor at zero
    temp = std::max(temp, 0.0f);
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
    // Branchless safe divide: compute A / safe_B, then zero out if |B| is near zero
    float abs_B = std::abs(B);
    // safe_B avoids actual hardware division-by-zero; sign-preserving epsilon
    float safe_B = B + ((B >= 0.0f) ? 1e-7f : -1e-7f);
    float result = A / safe_B;
    // Mask: 1.0 when |B| > threshold, 0.0 when |B| ≈ 0
    // This ensures divide-by-zero returns exactly 0
    float nonzero_mask = (abs_B > 1e-6f) ? 1.0f : 0.0f;
    st.across[provider.get(PortNames::o)] = result * nonzero_mask;
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
    // Convert any non-zero finite voltage to TRUE (including negative).
    // NaN → false (consistent with other gates where NaN is treated as "off").
    // Uses bit trick: shift left by 1 to drop sign bit, then compare.
    // Guard against NaN: std::isfinite check first.
    uint32_t b;
    std::memcpy(&b, &vin, sizeof(b));
    bool result = ((b + b) != 0) && std::isfinite(vin);
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

        {
            // Locale-independent parsing with whitespace tolerance
            std::string k_str = table_str.substr(pos, colon - pos);
            std::string v_str = table_str.substr(colon + 1, end - colon - 1);
            float kf, vf;
            if (!locale_safe::parse_float(k_str, kf) ||
                !locale_safe::parse_float(v_str, vf)) break;
            keys.push_back(kf);
            values.push_back(vf);
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
// Slider
// =============================================================================

template <typename Provider>
void Slider<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    // Pass control input directly to output (editor pushes value via signal_overrides_)
    float val = st.across[provider.get(PortNames::control)];
    st.across[provider.get(PortNames::out)] = val;
}

// =============================================================================
// Explicit Template Instantiation for JitProvider
// =============================================================================

#include "explicit_instantiations.h"
