#include "components/all.h"
#include "../state.h"
#include <spdlog/spdlog.h>

namespace an24 {

// =============================================================================
// Electrical Components
// =============================================================================

void Battery::solve_electrical(SimulationState& state, float /*dt*/) {
    float v_gnd = state.across[v_in_idx];   // ground side
    float v_bus = state.across[v_out_idx];  // bus side
    float g = inv_internal_r;

    spdlog::debug("[Battery {}] v_gnd={:.2f} v_bus={:.2f} g={:.2f}",
        name, v_gnd, v_bus, g);

    // Оптимизация: Thevenin→Norton: I = (V + V_gnd - V_bus) / R
    float i = (v_nominal + v_gnd - v_bus) * g;
    i = std::clamp(i, -1000.0f, 1000.0f);  // clamp to prevent instability

    spdlog::debug("[Battery {}] i={:.2f}", name, i);

    // Было:
    // state.through[v_out_idx] += i;
    // state.through[v_in_idx] -= i;
    // state.conductance[v_out_idx] += g;
    // state.conductance[v_in_idx] += g;
    // Стало - используем helper:
    stamp_two_port(state.conductance.data(), state.through.data(), state.across.data(),
                   v_out_idx, v_in_idx, g);
    // Корректируем ток (он уже посчитан с учетом V_nominal)
    state.through[v_out_idx] += v_nominal * g;
    state.through[v_in_idx] -= v_nominal * g;
}

void Battery::pre_load() {
    if (internal_r > 0.0f) {
        inv_internal_r = 1.0f / internal_r;
    }
}

void Switch::solve_electrical(SimulationState& state, float /*dt*/) {
    // Mirror downstream conductance onto v_in so upstream (battery) sees the load
    if (closed && downstream_g > 0.0f) {
        state.conductance[v_in_idx] += downstream_g;
        state.through[v_in_idx] -= state.across[v_in_idx] * downstream_g;
    }
}

void Switch::post_step(SimulationState& state, float /*dt*/) {
    float current_control = state.across[control_idx];

    if (std::abs(current_control - last_control) > 0.1f) {
        closed = !closed;
        spdlog::info("[Switch] Control changed {:.2f}→{:.2f}, toggled to closed={}",
            last_control, current_control, closed);
    }
    last_control = current_control;

    if (closed) {
        // Capture downstream conductance for next step
        downstream_g = state.conductance[v_out_idx];
        // Force voltage pass-through
        state.across[v_out_idx] = state.across[v_in_idx];
    } else {
        downstream_g = 0.0f;
        state.across[v_out_idx] = 0.0f;
    }

    state.across[state_idx] = closed ? 1.0f : 0.0f;
}

void Relay::solve_electrical(SimulationState& state, float /*dt*/) {
    v_out_old = state.across[v_out_idx];  // save before SOR
    // Mirror downstream Norton equivalent onto v_in so upstream sees the load
    if (closed && downstream_g > 0.0f) {
        state.conductance[v_in_idx] += downstream_g;
        state.through[v_in_idx] += downstream_I - state.across[v_in_idx] * downstream_g;
    }
}

void Relay::post_step(SimulationState& state, float /*dt*/) {
    float control_voltage = state.across[control_idx];
    closed = (control_voltage > hold_threshold);

    if (closed) {
        downstream_g = state.conductance[v_out_idx];
        // Use v_out_old (pre-SOR) for correct Norton current
        downstream_I = state.through[v_out_idx] + v_out_old * state.conductance[v_out_idx];
        state.across[v_out_idx] = state.across[v_in_idx];
    } else {
        downstream_g = 0.0f;
        downstream_I = 0.0f;
        state.across[v_out_idx] = 0.0f;
    }
}

void HoldButton::solve_electrical(SimulationState& state, float /*dt*/) {
    v_out_old = state.across[v_out_idx];  // save before SOR
    // Mirror downstream Norton equivalent onto v_in so upstream (battery) sees the load
    if (is_pressed && downstream_g > 0.0f) {
        state.conductance[v_in_idx] += downstream_g;
        state.through[v_in_idx] += downstream_I - state.across[v_in_idx] * downstream_g;
    }
}

void HoldButton::post_step(SimulationState& state, float /*dt*/) {
    float current = state.across[control_idx];

    if (std::abs(current - 1.0f) < 0.1f && std::abs(last_control - 1.0f) >= 0.1f) {
        is_pressed = true;
    } else if (std::abs(current - 2.0f) < 0.1f && std::abs(last_control - 2.0f) >= 0.1f) {
        is_pressed = false;
    }
    last_control = current;

    if (is_pressed) {
        downstream_g = state.conductance[v_out_idx];
        // Use v_out_old (pre-SOR) for correct Norton current
        downstream_I = state.through[v_out_idx] + v_out_old * state.conductance[v_out_idx];
        state.across[v_out_idx] = state.across[v_in_idx];
    } else {
        downstream_g = 0.0f;
        downstream_I = 0.0f;
        state.across[v_out_idx] = 0.0f;
    }

    state.across[state_idx] = is_pressed ? 1.0f : 0.0f;
}

void Resistor::solve_electrical(SimulationState& state, float /*dt*/) {
    // Было:
    // float v_in = state.across[v_in_idx];
    // float v_out = state.across[v_out_idx];
    // float i = (v_in - v_out) * conductance;
    // state.through[v_out_idx] += i;
    // state.through[v_in_idx] -= i;
    // state.conductance[v_out_idx] += conductance;
    // state.conductance[v_in_idx] += conductance;
    // Стало - используем helper (DRY, читаемость):
    stamp_two_port(state.conductance.data(), state.through.data(), state.across.data(),
                   v_out_idx, v_in_idx, conductance);
}

void Load::solve_electrical(SimulationState& state, float /*dt*/) {
    // Single port load to ground: I = V * g
    float v = state.across[input_idx];
    float i = v * conductance;  // current flowing to ground

    spdlog::debug("[Load] node={} v={:.2f} g={:.2f} i={:.2f}",
        input_idx, v, conductance, i);

    stamp_one_port_ground(state.conductance.data(), state.through.data(), state.across.data(),
                          input_idx, conductance);
}

void RefNode::solve_electrical(SimulationState& state, float /*dt*/) {
    // Fixed voltage reference
    // Add high conductance to make it a strong node
    float g = 1.0e6f;
    state.conductance[v_idx] += g;
    state.through[v_idx] += value * g;
}

void Generator::solve_electrical(SimulationState& state, float /*dt*/) {
    float g = (internal_r > 0.0f) ? 1.0f / internal_r : 0.0f;
    float v_gnd = state.across[v_in_idx];
    float v_bus = state.across[v_out_idx];

    // Same as battery: I = (V_nominal + V_gnd - V_bus) * G
    float i = (v_nominal + v_gnd - v_bus) * g;
    i = std::clamp(i, -1000.0f, 1000.0f);

    state.through[v_out_idx] += i;
    state.through[v_in_idx] -= i;

    state.conductance[v_out_idx] += g;
    state.conductance[v_in_idx] += g;
}

void GS24::solve_electrical(SimulationState& state, float /*dt*/) {
    float v_bus = state.across[v_out_idx];
    float v_gnd = state.across[v_in_idx];
    float rpm_percent = current_rpm / target_rpm;  // 0 to 1

    if (mode == GS24Mode::STARTER) {
        // === STARTER MODE ===
        // I_cons = (U_bus - K_motor * RPM) / R_internal
        // Back-EMF reduces current as RPM increases
        float back_emf = k_motor * current_rpm;  // proportional to RPM

        // Calculate consumed current (can be negative if back_emf > v_bus)
        float i_consumed = (v_bus - back_emf) / r_internal;

        // Clamp: if positive, it's consumption; if negative, limit to small value
        // (real starter can't be negative until mode switch)
        if (i_consumed < 50.0f) i_consumed = 50.0f;  // minimum 50A hold
        if (i_consumed > i_max_starter) i_consumed = i_max_starter;

        float g_internal = 1.0f / r_internal;

        spdlog::debug("[GS24 starter] v_bus={:.1f} back_emf={:.1f} I={:.0f}A mode=STARTER",
            v_bus, back_emf, i_consumed);

        // Current flows from bus to ground (consuming power)
        state.through[v_out_idx] -= i_consumed;
        state.through[v_in_idx] += i_consumed;
        state.conductance[v_out_idx] += g_internal;

    } else if (mode == GS24Mode::GENERATOR) {
        // === GENERATOR MODE (Norton) ===
        // I_no = I_max * Phi(RPM) * k_mod
        // Phi(RPM): 0 if <40%, linear 40-60%, 1 if >60%
        float phi = 0.0f;
        if (rpm_percent >= 0.6f) {
            phi = 1.0f;
        } else if (rpm_percent >= rpm_threshold) {
            phi = (rpm_percent - rpm_threshold) / 0.2f;
        }

        // Get k_mod from RUG82 (default 1.0 if not connected)
        float k_mod = (k_mod_idx > 0) ? state.across[k_mod_idx] : 1.0f;

        // Norton current source
        float i_no = i_max * phi * k_mod;
        i_no = std::clamp(i_no, 0.0f, 100.0f);

        float g_norton = (r_norton > 0.0f) ? 1.0f / r_norton : 0.0f;

        spdlog::debug("[GS24 gen] rpm%={:.0f}% phi={:.2f} k_mod={:.2f} I_no={:.1f}A mode=GENERATOR",
            rpm_percent * 100, phi, k_mod, i_no);

        // Norton: current injected to bus node
        state.through[v_out_idx] += i_no;
        state.conductance[v_out_idx] += g_norton;

    } else {
        // OFF or STARTER_WAIT - no current
        (void)v_bus;
        (void)v_gnd;
        (void)rpm_percent;
    }
}

void GS24::post_step(SimulationState& state, float dt) {
    (void)state;

    float rpm_percent = current_rpm / target_rpm;

    // State machine transitions
    switch (mode) {
        case GS24Mode::STARTER:
            // Accelerate while in starter mode
            if (current_rpm < target_rpm * rpm_cutoff) {
                float acceleration = 300.0f;  // RPM per second
                current_rpm += acceleration * dt;
            }

            // Debug: show RPM progress
            spdlog::debug("[GS24 post] rpm={:.0f} ({:.1f}%) cutoff={:.1f}",
                current_rpm, rpm_percent * 100, rpm_cutoff * 100);

            // Check for transition to generator (45% RPM)
            if (rpm_percent >= rpm_cutoff) {
                current_rpm = target_rpm * rpm_cutoff;
                mode = GS24Mode::STARTER_WAIT;
                wait_time = 0.0f;
                spdlog::info("[GS24] STARTER -> STARTER_WAIT at {:.0f}% RPM", rpm_percent * 100);
            }
            break;

        case GS24Mode::STARTER_WAIT:
            // Brief pause before switching to generator
            wait_time += dt;
            if (wait_time >= 1.0f) {  // 1 second wait
                mode = GS24Mode::GENERATOR;
                spdlog::info("[GS24] STARTER_WAIT -> GENERATOR");
            }
            break;

        case GS24Mode::GENERATOR:
            // Continue accelerating to target RPM
            if (current_rpm < target_rpm) {
                float acceleration = 500.0f;
                current_rpm += acceleration * dt;
                if (current_rpm > target_rpm) current_rpm = target_rpm;
            }
            break;

        case GS24Mode::OFF:
        default:
            // No action
            break;
    }
}

void Transformer::solve_electrical(SimulationState& state, float /*dt*/) {
    // Ideal transformer with voltage ratio
    float v_primary = state.across[primary_idx];
    float v_secondary = v_primary * ratio;

    // Use conductance to create current source behavior
    // V = I/G => I = V * G
    // We want v_secondary, so I = v_secondary * G
    float g = 1.0f;

    // Add conductance to both sides
    state.conductance[primary_idx] += g;
    state.conductance[secondary_idx] += g;

    // Current source: I = V_target * G
    float i = v_secondary * g;
    state.through[secondary_idx] += i;
    state.through[primary_idx] -= i;
}

void Inverter::solve_electrical(SimulationState& state, float /*dt*/) {
    // DC-AC inverter
    float v_dc = state.across[dc_in_idx];

    // Simplified: output follows input with efficiency
    float v_ac = v_dc * efficiency;

    float g = 1.0f;
    state.conductance[ac_out_idx] += g;
    state.through[ac_out_idx] += v_ac * g;
}

void LerpNode::solve_electrical(SimulationState& state, float /*dt*/) {
    // First-order lag: output follows input with factor
    // This is the electrical connection (for voltage signals)
    float v_input = state.across[input_idx];
    float v_output = state.across[output_idx];

    // Output conductance
    float g = 1.0f;
    state.conductance[output_idx] += g;

    // Current based on difference
    float i = (v_input - v_output) * factor * g;
    state.through[output_idx] += i;
}

void LerpNode::post_step(SimulationState& state, float dt) {
    // First-order filter: output = output + factor * (input - output)
    // This provides thermal inertia / sensor lag
    // factor = 0.1 means 10% of the difference is applied each step
    // At 60Hz, this gives ~6x per second, so settling time is about 1/factor seconds
    float v_input = state.across[input_idx];
    float v_output = state.across[output_idx];

    // Apply filter
    float new_output = v_output + factor * (v_input - v_output);
    state.across[output_idx] = new_output;
}

void IndicatorLight::solve_electrical(SimulationState& state, float /*dt*/) {
    // Light draws power between v_in (bus) and v_out (return/ground)
    float v_in = state.across[v_in_idx];
    float v_out = state.across[v_out_idx];
    float v_diff = v_in - v_out;

    // Conductance (resistive load) - ~10W at 28V ≈ 0.35 S
    float g = 0.35f;
    float i = v_diff * g;

    state.through[v_out_idx] += i;    // current into return node
    state.through[v_in_idx] -= i;     // current out of bus node
    state.conductance[v_out_idx] += g;
    state.conductance[v_in_idx] += g;

    // Brightness: normalized 0-1 based on voltage difference
    float normalized = std::clamp(v_diff / 28.0f, 0.0f, 1.0f);
    float brightness = normalized * max_brightness;
    state.across[brightness_idx] = brightness;
}

void HighPowerLoad::solve_electrical(SimulationState& state, float /*dt*/) {
    // Constant power load: P = V * I, so I = P / V
    float v_in = state.across[v_in_idx];
    float v_out = state.across[v_out_idx];
    float v_diff = v_in - v_out;

    if (v_diff > 0.01f) {
        float i = power_draw / v_diff;
        state.through[v_out_idx] += i;
        state.through[v_in_idx] -= i;

        float g = i / v_diff;
        state.conductance[v_out_idx] += g;
        state.conductance[v_in_idx] += g;
    }
}

void Gyroscope::solve_electrical(SimulationState& state, float /*dt*/) {
    // Power-only sensor, no output
    float v_input = state.across[input_idx];
    state.conductance[input_idx] += conductance;
    state.through[input_idx] -= v_input * conductance;
}

void AGK47::solve_electrical(SimulationState& state, float /*dt*/) {
    // Similar to gyroscope
    float v_input = state.across[input_idx];
    state.conductance[input_idx] += conductance;
    state.through[input_idx] -= v_input * conductance;
}

// =============================================================================
// Hydraulic Components
// =============================================================================

void ElectricPump::solve_electrical(SimulationState& state, float /*dt*/) {
    // Electric motor: draws power based on pressure
    float v_in = state.across[v_in_idx];
    float g = 0.01f;
    state.conductance[v_in_idx] += g;
    state.through[v_in_idx] -= v_in * g;
}

void ElectricPump::solve_hydraulic(SimulationState& state, float /*dt*/) {
    // Hydraulic pump: outputs pressure based on voltage
    float v_in = state.across[v_in_idx];
    float p_out = state.across[p_out_idx];

    // Simplified: pressure proportional to voltage
    float target_p = v_in * max_pressure / 28.0f;  // 28V = max pressure

    float g = 1.0f;
    state.conductance[p_out_idx] += g;
    state.through[p_out_idx] += target_p * g;
}

void SolenoidValve::solve_hydraulic(SimulationState& state, float /*dt*/) {
    // Check control signal
    float ctrl = state.across[ctrl_idx];
    bool open = (ctrl > 12.0f) ^ normally_closed;  // 12V opens

    if (open) {
        // Low resistance when open
        float g = 1.0e6f;
        state.conductance[flow_in_idx] += g;
        state.conductance[flow_out_idx] += g;
    }
}

// =============================================================================
// Mechanical Components
// =============================================================================

void InertiaNode::solve_mechanical(SimulationState& state, float /*dt*/) {
    // First-order lag: output follows input with time constant
    float v_input = state.across[input_idx];
    float v_output = state.across[output_idx];

    // Conductance based on mass
    float g = damping;
    state.conductance[output_idx] += g;

    // Current proportional to difference
    float i = (v_input - v_output) * inv_mass * g;
    state.through[output_idx] += i;
}

void InertiaNode::pre_load() {
    if (mass > 0.0f) {
        inv_mass = 1.0f / mass;
    }
}

// =============================================================================
// Thermal Components
// =============================================================================

void TempSensor::solve_thermal(SimulationState& state, float /*dt*/) {
    // Temperature sensor: output follows input with sensitivity
    float temp_in = state.across[temp_in_idx];
    state.across[temp_out_idx] = temp_in * sensitivity;
}

void ElectricHeater::solve_electrical(SimulationState& state, float /*dt*/) {
    // Heater draws power
    float v_in = state.across[power_idx];
    float g = max_power / (v_in * v_in + 0.01f);
    state.conductance[power_idx] += g;
    state.through[power_idx] -= v_in * g * efficiency;
}

void ElectricHeater::solve_thermal(SimulationState& state, float /*dt*/) {
    // Converts electrical power to heat
    float v_in = state.across[power_idx];
    float heat_power = v_in * v_in * efficiency;
    state.through[heat_out_idx] += heat_power;
}

// =============================================================================
// RUG-82 Voltage Regulator
// =============================================================================

void RUG82::solve_electrical(SimulationState& state, float dt) {
    // Read generator voltage (bus voltage)
    float v_gen = state.across[v_gen_idx];

    // Calculate error from target (28.5V)
    float error = v_target - v_gen;

    // RUG-82: if V too high, decrease k_mod (reduce excitation)
    // if V too low, increase k_mod (increase excitation)
    k_mod += kp * error * dt;

    // Clamp to 0...1
    if (k_mod < 0.0f) k_mod = 0.0f;
    if (k_mod > 1.0f) k_mod = 1.0f;

    spdlog::debug("[RUG82] v_gen={:.2f} error={:.2f} k_mod={:.2f}",
        v_gen, error, k_mod);

    // Write k_mod to output
    state.across[k_mod_idx] = k_mod;
}

// =============================================================================
// DMR-400 Differential Minimum Relay
// =============================================================================

void DMR400::solve_electrical(SimulationState& st, float /*dt*/) {
    if (!is_closed) {
        // Contactor open - no connection
        return;
    }

    // When closed, connect generator to bus with low resistance (wire)
    // In Norton model: add parallel conductance between the two nodes
    float g_closed = 100.0f;  // ~0.01 Ohm when closed

    // Add conductance to both nodes (creates connection in the matrix)
    st.conductance[v_gen_ref_idx] += g_closed;
    st.conductance[v_out_idx] += g_closed;

    // Also add current injection to balance the voltages
    // This helps the solver converge faster
    float v_avg = (st.across[v_gen_ref_idx] + st.across[v_out_idx]) * 0.5f;
    st.through[v_gen_ref_idx] += (v_avg - st.across[v_gen_ref_idx]) * g_closed;
    st.through[v_out_idx] += (v_avg - st.across[v_out_idx]) * g_closed;
}

void DMR400::post_step(SimulationState& st, float dt) {
    float v_gen = st.across[v_gen_ref_idx];
    float v_bus = st.across[v_in_idx];

    // Update reconnect delay
    if (reconnect_delay > 0.0f) {
        reconnect_delay -= dt;
    }

    if (!is_closed) {
        // Can close if: V_gen > V_bus + threshold AND V_gen > min_voltage
        // (min_voltage ensures starter is done)
        if (reconnect_delay <= 0.0f && v_gen > v_bus + connect_threshold && v_gen > min_voltage_to_close) {
            is_closed = true;
            spdlog::info("[DMR400] CONNECTED: V_gen={:.1f} > V_bus={:.1f} + {:.1f}, V_gen > {:.1f}V",
                v_gen, v_bus, connect_threshold, min_voltage_to_close);
        }
    } else {
        // Check for reverse current: V_bus significantly higher than V_gen
        if (v_bus > v_gen + disconnect_threshold) {
            is_closed = false;
            reconnect_delay = 1.0f;  // 1 second delay before reconnect
            spdlog::warn("[DMR400] DISCONNECTED: Reverse current V_bus={:.1f} > V_gen={:.1f}",
                v_bus, v_gen);
        }
    }

    // Warning lamp: ON when disconnected, OFF when connected
    if (lamp_idx > 0) {
        st.across[lamp_idx] = is_closed ? 0.0f : 1.0f;
    }
}

// =============================================================================
// RU19A-300 APU (ВСУ)
// =============================================================================

void RU19A::solve_electrical(SimulationState& st, float /*dt*/) {
    float v_start = st.across[v_start_idx];
    float v_bus = st.across[v_bus_idx];

    spdlog::debug("[RU19A] solve_electrical: state={} v_start={:.2f}V v_bus={:.2f}V",
        (int)this->state, v_start, v_bus);

    if (this->state == APUState::OFF) {
        // No output - waiting for start
        return;
    }

    if (this->state == APUState::CRANKING || this->state == APUState::IGNITION) {
        // === STARTER MODE ===
        // GS24 acting as starter - consumes power from v_start (bypasses DMR)
        // Parameters: R_internal = 0.025 Ohm, k_motor = 38.0 V at 100% RPM
        constexpr float R_START_INTERNAL = 0.025f;
        constexpr float K_MOTOR_BACK_EMF = 38.0f;

        float rpm_percent = current_rpm / target_rpm;
        float back_emf = K_MOTOR_BACK_EMF * rpm_percent;
        float i_consumed = (v_start - back_emf) / R_START_INTERNAL;

        // Physical limits: 0A at stall, up to 1000A on weak batteries
        if (i_consumed < 0.0f) i_consumed = 0.0f;
        if (i_consumed > 1000.0f) i_consumed = 1000.0f;

        spdlog::debug("[RU19A] starter mode: v_start={:.1f} back_emf={:.1f} I={:.0f}A",
            v_start, back_emf, i_consumed);

        // Consume current from v_start (direct battery connection, bypasses DMR)
        st.through[v_start_idx] -= i_consumed;
        st.conductance[v_start_idx] += 1.0f / R_START_INTERNAL;

    } else if (this->state == APUState::RUNNING) {
        // === GENERATOR MODE ===
        // GS24 acting as generator - produces power to bus (through DMR)
        float rpm_percent = current_rpm / target_rpm;

        // Phi(RPM): 0 if <40%, linear 40-60%, 1 if >60%
        float phi = 0.0f;
        if (rpm_percent >= 0.6f) {
            phi = 1.0f;
        } else if (rpm_percent >= 0.4f) {
            phi = (rpm_percent - 0.4f) / 0.2f;
        }

        // Get k_mod from RUG82
        float k_mod = st.across[k_mod_idx];

        // Norton current source
        float i_no = 400.0f * phi * k_mod;  // I_max = 400A
        if (i_no > 100.0f) i_no = 100.0f;  // limit for stability

        float g_norton = 1.0f / 0.08f;  // R_norton = 0.08

        spdlog::debug("[RU19A] generator mode: rpm={:.0f}% phi={:.2f} I={:.1f}A",
            rpm_percent * 100, phi, i_no);

        // Inject current to bus
        st.through[v_bus_idx] += i_no;
        st.conductance[v_bus_idx] += g_norton;
    }
}

// Thermal solver (runs at 1 Hz)
void RU19A::solve_mechanical(SimulationState& st, float dt) {
    spdlog::info("[RU19A] solve_mechanical called! state={}, current_rpm={:.0f}", (int)this->state, current_rpm);


    // Determine target RPM based on APU state
    float target_rpm_local = 0.0f;
    float voltage_factor = 1.0f;  // Default: full power

    switch (this->state) {
        case APUState::OFF:
        case APUState::STOPPING:
            target_rpm_local = 0.0f;
            break;
        case APUState::CRANKING: {
            // Cranking RPM depends on battery voltage
            float v_bus = st.across[v_bus_idx];
            voltage_factor = std::clamp(v_bus / 24.0f, 0.5f, 1.0f);
            target_rpm_local = 2000.0f * voltage_factor;
            break;
        }
        case APUState::IGNITION:
            target_rpm_local = 5000.0f;
            break;
        case APUState::RUNNING:
            // Idle RPM is 60% of max
            target_rpm_local = target_rpm * 0.6f;
            break;
    }

    // Применяем инерцию турбины
    // Раскрутка: быстрое ускорение, Торможение: медленное замедление
    float inertia = (target_rpm_local > current_rpm) ? spinup_inertia : spindown_inertia;

    // Нелинейность: быстрее на средних оборотах (как в реальной жизни)
    // Турбина разгоняется медленно на старте, быстро в середине, плавно на максимуме
    if (target_rpm_local > current_rpm && target_rpm_local > 100.0f) {
        float progress = current_rpm / target_rpm_local;  // 0.0 - 1.0
        // Парабола: 1.0 на краях, 2.5 в середине - разгон быстрее в середине диапазона
        float nonlinearity = 1.0f + 3.0f * progress * (1.0f - progress);
        current_rpm += (target_rpm_local - current_rpm) * dt * inertia * nonlinearity;
    } else if (target_rpm_local < current_rpm) {
        // Торможение: быстрее чем раскрутка (турбина быстрее останавливается)
        current_rpm += (target_rpm_local - current_rpm) * dt * inertia * 2.0f;
    } else {
        // Линейное приближение для малых оборотов
        current_rpm += (target_rpm_local - current_rpm) * dt * inertia;
    }

    // Clamp to valid range
    if (current_rpm < 0.0f) current_rpm = 0.0f;
    if (current_rpm > target_rpm) current_rpm = target_rpm;

    // Output RPM to port
    st.across[rpm_out_idx] = current_rpm;
}

void RU19A::solve_thermal(SimulationState& st, float dt) {
    spdlog::info("[RU19A] solve_thermal: state={} t4={:.1f} target={:.1f}",
        (int)this->state, t4, (this->state == APUState::RUNNING) ? t4_target :
                           (this->state == APUState::IGNITION) ? 150.0f : ambient_temp);

    (void)st;

    // Simple thermal model: approach target temperature with thermal inertia
    float target_temp = ambient_temp;  // Default: cool to ambient

    // Cooling is faster than heating (no combustion, air still flowing)
    constexpr float THERMAL_INERTIA_HEATING = 0.2f;    // ~5 sec to 63% of target
    constexpr float THERMAL_INERTIA_COOLING = 0.1f;    // ~10 sec cooldown

    float inertia = THERMAL_INERTIA_COOLING;  // Default: faster cooling

    if (this->state == APUState::IGNITION) {
        target_temp = 150.0f;  // Moderate temp during ignition
        inertia = THERMAL_INERTIA_HEATING;
    } else if (this->state == APUState::RUNNING) {
        target_temp = t4_target;  // Full idle temp (400°C)
        inertia = THERMAL_INERTIA_HEATING;
    }

    t4 += (target_temp - t4) * dt * inertia;

    // HOT START CHECK: Emergency abort if T4 exceeds limit
    if (t4 > t4_max) {
        spdlog::warn("[RU19A] HOT START ABORT! T4={:.0f}C > {:.0f}C", t4, t4_max);
        this->state = APUState::STOPPING;
    }

    // Output temperature to port
    st.across[t4_out_idx] = t4;
}

void RU19A::post_step(SimulationState& st, float dt) {
    float v_start = st.across[v_start_idx];
    float v_bus = st.across[v_bus_idx];
    timer += dt;

    // Note: solve_mechanical() and solve_thermal() are called automatically
    // by domain systems at 20 Hz and 1 Hz respectively

    switch (this->state) {
        case APUState::OFF: {
            // Reset thermal state
            t4 = ambient_temp;
            timer = 0.0f;

            // Auto-start if enabled and battery connected
            // Check both v_start (new) and v_bus (legacy) for compatibility
            float start_voltage = (v_start_idx > 0) ? st.across[v_start_idx] : v_bus;
            spdlog::debug("[RU19A] OFF state: auto_start={}, v_start_idx={}, start_voltage={:.2f}",
                auto_start, v_start_idx, start_voltage);
            if (auto_start && start_voltage > 10.0f) {
                this->state = APUState::CRANKING;
                spdlog::info("[RU19A] AUTOSTART - CRANKING");
            }
            break;
        }

        case APUState::CRANKING: {
            // CRANKING работает автономно после импульса запуска - НЕ проверяем v_start
            // (кнопка "Запуск" это импульс, не удержание)

            // Check if cranking time is complete
            if (timer >= crank_time) {
                this->state = APUState::IGNITION;
                timer = 0.0f;
                spdlog::info("[RU19A] CRANKING -> IGNITION");
            }
            break;
        }

        case APUState::IGNITION: {
            // IGNITION работает автономно - НЕ проверяем v_start

            // Check if ignition time is complete
            if (timer >= ignition_time) {
                this->state = APUState::RUNNING;
                timer = 0.0f;
                spdlog::info("[RU19A] IGNITION -> RUNNING");
            }

            // Timeout check
            if (timer > start_timeout) {
                this->state = APUState::STOPPING;
                spdlog::warn("[RU19A] Start timeout!");
            }
            break;
        }

        case APUState::RUNNING: {
            // APU работает автономно и НЕ останавливается автоматически
            // (останов только через внешнюю команду или когда кончится топливо в модели)

            // TODO: TODO: TODO
            // НУЖНА ПОЛНОЦЕННАЯ STATE MACHINE ДЛЯ ОТКАЗОВ!
            // Если в RUNNING RPM упали ниже 50% от idle (9600 * 0.5 = 4800) - это отказ
            // Возможные отказы:
            // - Помпаж (surge) - резкое падение RPM
            // - Потеря пламени (flameout) - медленное падение RPM
            // - Механический отказ - остановка турбины
            // При отказе: transition to STOPPING или SHUTDOWN (разные режимы)

            break;
        }

        case APUState::STOPPING: {
            // Check if fully stopped
            if (current_rpm <= 0.1f) {  // Small threshold
                current_rpm = 0.0f;
                this->state = APUState::OFF;
                spdlog::info("[RU19A] STOPPED");
            }
            break;
        }
    }

    // Output RPM for instrumentation
    float rpm_percent = current_rpm / target_rpm;
    if (rpm_out_idx > 0) {
        st.across[rpm_out_idx] = rpm_percent * 100.0f;  // as percentage
    }

    // Output T4 for instrumentation
    if (t4_out_idx > 0) {
        st.across[t4_out_idx] = t4;
    }
}

void Radiator::solve_thermal(SimulationState& state, float /*dt*/) {
    // Heat exchanger: cools the input
    float heat_in = state.across[heat_in_idx];
    float heat_out = state.across[heat_out_idx];

    float g = cooling_capacity;
    state.conductance[heat_in_idx] += g;
    state.conductance[heat_out_idx] += g;

    // Heat flows from hot to cold
    float delta = heat_in - heat_out;
    state.through[heat_in_idx] += delta * g;
    state.through[heat_out_idx] -= delta * g;
}

} // namespace an24
