#include "components/all.h"
#include "../state.h"
#include <spdlog/spdlog.h>

namespace an24 {

// =============================================================================
// Electrical Components
// =============================================================================

void Battery::solve_electrical(SimulationState& state) {
    float v_gnd = state.across[v_in_idx];   // ground side
    float v_bus = state.across[v_out_idx];  // bus side
    float g = inv_internal_r;

    spdlog::debug("[Battery {}] v_gnd={:.2f} v_bus={:.2f} g={:.2f}",
        name, v_gnd, v_bus, g);

    // Current: I = (V_nominal + V_gnd - V_bus) / R
    float i = (v_nominal + v_gnd - v_bus) * g;
    i = std::clamp(i, -1000.0f, 1000.0f);  // clamp to prevent instability

    spdlog::debug("[Battery {}] i={:.2f}", name, i);

    // Add to through currents
    state.through[v_out_idx] += i;
    state.through[v_in_idx] -= i;

    // Add conductance
    state.conductance[v_out_idx] += g;
    state.conductance[v_in_idx] += g;
}

void Battery::pre_load() {
    if (internal_r > 0.0f) {
        inv_internal_r = 1.0f / internal_r;
    }
}

void Relay::solve_electrical(SimulationState& state) {
    if (!closed) return;  // open relay: no connection

    // Closed relay = wire: add very high conductance between in and out
    float g = 1.0e6f;
    state.conductance[v_in_idx] += g;
    state.conductance[v_out_idx] += g;
    // Current flows from in to out: I = (V_in - V_out) * g
    float i = (state.across[v_in_idx] - state.across[v_out_idx]) * g;
    state.through[v_in_idx] -= i;
    state.through[v_out_idx] += i;
}

void Relay::post_step(SimulationState& state, float /*dt*/) {
    if (!closed) return;  // open relay: no connection

    // Closed relay = wire: just copy voltage from in to out.
    // Skips SOR convergence entirely — no conductance needed.
    state.across[v_out_idx] = state.across[v_in_idx];
}

void Resistor::solve_electrical(SimulationState& state) {
    float v_in = state.across[v_in_idx];
    float v_out = state.across[v_out_idx];

    // Current based on voltage difference: I = (V_in - V_out) * G
    float i = (v_in - v_out) * conductance;

    state.through[v_out_idx] += i;
    state.through[v_in_idx] -= i;
    state.conductance[v_out_idx] += conductance;
    state.conductance[v_in_idx] += conductance;
}

void Load::solve_electrical(SimulationState& state) {
    // Single port load to ground: I = V * g
    float v = state.across[node_idx];
    float i = v * conductance;  // current flowing to ground

    spdlog::debug("[Load] node={} v={:.2f} g={:.2f} i={:.2f}",
        node_idx, v, conductance, i);

    state.through[node_idx] -= i;  // current flows out of node to ground
    state.conductance[node_idx] += conductance;
}

void RefNode::solve_electrical(SimulationState& state) {
    // Fixed voltage reference
    // Add high conductance to make it a strong node
    float g = 1.0e6f;
    state.conductance[node_idx] += g;
    state.through[node_idx] += value * g;
}

void Generator::solve_electrical(SimulationState& state) {
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

void Transformer::solve_electrical(SimulationState& state) {
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

void Inverter::solve_electrical(SimulationState& state) {
    // DC-AC inverter
    float v_dc = state.across[dc_in_idx];

    // Simplified: output follows input with efficiency
    float v_ac = v_dc * efficiency;

    float g = 1.0f;
    state.conductance[ac_out_idx] += g;
    state.through[ac_out_idx] += v_ac * g;
}

void LerpNode::solve_electrical(SimulationState& state) {
    // First-order lag: output follows input with factor
    float v_input = state.across[input_idx];
    float v_output = state.across[output_idx];

    // Output conductance
    float g = 1.0f;
    state.conductance[output_idx] += g;

    // Current based on difference
    float i = (v_input - v_output) * factor * g;
    state.through[output_idx] += i;
}

void IndicatorLight::solve_electrical(SimulationState& state) {
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

void HighPowerLoad::solve_electrical(SimulationState& state) {
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

void Gyroscope::solve_electrical(SimulationState& state) {
    // Power-only sensor, no output
    float v_input = state.across[input_idx];
    float g = 0.001f;  // small load
    state.conductance[input_idx] += g;
    state.through[input_idx] -= v_input * g;
}

void AGK47::solve_electrical(SimulationState& state) {
    // Similar to gyroscope
    float v_input = state.across[input_idx];
    float g = 0.001f;
    state.conductance[input_idx] += g;
    state.through[input_idx] -= v_input * g;
}

// =============================================================================
// Hydraulic Components
// =============================================================================

void ElectricPump::solve_electrical(SimulationState& state) {
    // Electric motor: draws power based on pressure
    float v_in = state.across[v_in_idx];
    float g = 0.01f;
    state.conductance[v_in_idx] += g;
    state.through[v_in_idx] -= v_in * g;
}

void ElectricPump::solve_hydraulic(SimulationState& state) {
    // Hydraulic pump: outputs pressure based on voltage
    float v_in = state.across[v_in_idx];
    float p_out = state.across[p_out_idx];

    // Simplified: pressure proportional to voltage
    float target_p = v_in * max_pressure / 28.0f;  // 28V = max pressure

    float g = 1.0f;
    state.conductance[p_out_idx] += g;
    state.through[p_out_idx] += target_p * g;
}

void SolenoidValve::solve_hydraulic(SimulationState& state) {
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

void InertiaNode::solve_mechanical(SimulationState& state) {
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

void TempSensor::solve_thermal(SimulationState& state) {
    // Temperature sensor: output follows input with sensitivity
    float temp_in = state.across[temp_in_idx];
    state.across[temp_out_idx] = temp_in * sensitivity;
}

void ElectricHeater::solve_electrical(SimulationState& state) {
    // Heater draws power
    float v_in = state.across[power_idx];
    float g = max_power / (v_in * v_in + 0.01f);
    state.conductance[power_idx] += g;
    state.through[power_idx] -= v_in * g * efficiency;
}

void ElectricHeater::solve_thermal(SimulationState& state) {
    // Converts electrical power to heat
    float v_in = state.across[power_idx];
    float heat_power = v_in * v_in * efficiency;
    state.through[heat_out_idx] += heat_power;
}

void Radiator::solve_thermal(SimulationState& state) {
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
