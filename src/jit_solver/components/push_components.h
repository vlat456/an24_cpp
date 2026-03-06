#pragma once

#include "component.h"
#include <cstdint>
#include <string>
#include <limits>

namespace an24 {

// Forward declaration
class PushState;

/// Push-based Switch - propagates voltage when closed
class PushSwitch : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    uint32_t control_idx = 0;
    uint32_t state_idx = 0;

    bool closed = false;
    float last_control = 0.0f;
    float r_closed = 0.01f;  // 10mΩ when closed

    PushSwitch() = default;
    PushSwitch(uint32_t v_in, uint32_t v_out, uint32_t control, uint32_t state, bool is_closed = false)
        : v_in_idx(v_in), v_out_idx(v_out), control_idx(control), state_idx(state), closed(is_closed) {}

    [[nodiscard]] std::string_view type_name() const override { return "Switch"; }

    /// Push voltage: if closed, V_out ≈ V_in; if open, V_out = 0
    void push_voltage(PushState& state) {
        float v_in = state.get_voltage("v_in");

        if (closed) {
            // Closed: propagate with small voltage drop
            // I = V_in / R_load (computing downstream current)
            // V_out = V_in - I * R_wire
            state.set_voltage("v_out", v_in);  // TODO: add voltage drop
        } else {
            // Open: no voltage pass-through
            state.set_voltage("v_out", 0.0f);
        }
    }

    /// Update state from control signal (edge detection)
    void update_state(PushState& state) {
        float v_control = state.get_voltage("control");

        // Toggle on any change (0 → 1 or 1 → 0)
        if (std::abs(v_control - last_control) > 0.1f) {
            closed = !closed;
        }
        last_control = v_control;

        // Output state: 1V = closed, 0V = open
        state.set_voltage("state", closed ? 1.0f : 0.0f);
    }
};

/// Push-based HoldButton - press-and-hold button
class PushHoldButton : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    uint32_t control_idx = 0;
    uint32_t state_idx = 0;

    bool is_pressed = false;
    float last_control = 0.0f;
    float r_closed = 0.01f;  // 10mΩ when pressed

    PushHoldButton() = default;
    PushHoldButton(uint32_t v_in, uint32_t v_out, uint32_t control, uint32_t state)
        : v_in_idx(v_in), v_out_idx(v_out), control_idx(control), state_idx(state) {}

    [[nodiscard]] std::string_view type_name() const override { return "HoldButton"; }

    /// Push voltage: if pressed, V_out ≈ V_in
    void push_voltage(PushState& state) {
        float v_in = state.get_voltage("v_in");

        if (is_pressed) {
            state.set_voltage("v_out", v_in);
        } else {
            state.set_voltage("v_out", 0.0f);
        }
    }

    /// Update state from control signal (1.0V = press, 2.0V = release)
    void update_state(PushState& state) {
        float v_control = state.get_voltage("control");

        // Edge detection: 0→1 = press, 1→2 = release
        if (std::abs(v_control - 1.0f) < 0.1f && std::abs(last_control - 1.0f) >= 0.1f) {
            is_pressed = true;
        } else if (std::abs(v_control - 2.0f) < 0.1f && std::abs(last_control - 2.0f) >= 0.1f) {
            is_pressed = false;
        }
        last_control = v_control;

        // Output state: 1V = pressed, 0V = released
        state.set_voltage("state", is_pressed ? 1.0f : 0.0f);
    }
};

/// Push-based Battery - voltage source with internal resistance
class PushBattery : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;   // Ground reference
    uint32_t v_out_idx = 0;  // Output

    float v_nominal = 28.0f;    // Nominal voltage
    float internal_r = 0.01f;   // Internal resistance (10mΩ)
    float charge = 1000.0f;     // Current charge (Ah)

    PushBattery() = default;
    PushBattery(uint32_t v_in, uint32_t v_out, float v_nom, float r_int, bool load = false)
        : v_in_idx(v_in), v_out_idx(v_out), v_nominal(v_nom), internal_r(r_int) {
        (void)load;  // No longer needed, we use downstream resistance
    }

    [[nodiscard]] std::string_view type_name() const override { return "Battery"; }

    /// Push voltage: V_out = V_nominal - I * R_internal
    /// Current I depends on downstream load resistance
    /// Charging mode: if V_in > V_nominal, battery charges
    void push_voltage(PushState& state) {
        float v_in = state.signals[v_in_idx].voltage;

        // Check if charging (V_in > V_nominal) or discharging
        bool is_charging = (v_in > v_nominal);

        float v_out;
        if (is_charging) {
            // Charging mode: V_out ≈ V_in (diode drop)
            // Current flows INTO battery: I = (V_in - V_nominal) / R_internal
            float i_charge = (v_in - v_nominal) / internal_r;
            // V_out = V_in - I * R_internal = V_in - (V_in - V_nominal) = V_nominal
            // But with charging, output should be slightly above nominal
            v_out = v_nominal + (i_charge * internal_r * 0.1f);  // Small charging boost
        } else {
            // Discharging mode
            // Get downstream load resistance
            float r_load = state.signals[v_out_idx].resistance;

            // If resistance is very low (wire/short circuit), treat as no load
            // Wires shouldn't cause voltage sag - they're just conductors
            if (r_load < 1.0f) {
                r_load = 1e9f;  // 1 GΩ = essentially open circuit
            }

            // I = V_nominal / (R_internal + R_load)
            float i_load = v_nominal / (internal_r + r_load);

            // Voltage sag due to internal resistance
            v_out = v_nominal - (i_load * internal_r);
        }

        // Set output voltage using index
        state.signals[v_out_idx].voltage = v_out;
    }
};

/// Push-based IndicatorLight - simple resistive load
class PushIndicatorLight : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    uint32_t brightness_idx = 0;

    float r_on = 100.0f;  // 100Ω when lit
    float v_threshold = 1.0f;  // Turn on above 1V

    PushIndicatorLight() = default;
    PushIndicatorLight(uint32_t v_in, uint32_t v_out, uint32_t brightness)
        : v_in_idx(v_in), v_out_idx(v_out), brightness_idx(brightness) {
        // TODO: remove these when we parse params properly
        (void)v_in; (void)v_out; (void)brightness;
    }

    [[nodiscard]] std::string_view type_name() const override { return "IndicatorLight"; }

    /// Set resistance at input (load resistance)
    void propagate_resistance(PushState& state) {
        state.signals[v_in_idx].resistance = r_on;
    }

    /// Push voltage: V_out = V_in - I * R
    /// Brightness = V_in / V_threshold (clamped 0-1)
    void push_voltage(PushState& state) {
        float v_in = state.signals[v_in_idx].voltage;

        // I = V_in / R
        float i = v_in / r_on;

        // V_out = V_in - I * R = V_in - V_in = 0V (all voltage dropped across lamp)
        state.signals[v_out_idx].voltage = 0.0f;

        // Brightness based on input voltage
        float brightness = std::clamp(v_in / v_threshold, 0.0f, 1.0f);
        state.signals[brightness_idx].voltage = brightness;
    }
};

/// Push-based Resistor - simple resistive load
class PushResistor : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;

    float resistance = 100.0f;  // Ohms

    PushResistor() = default;
    PushResistor(uint32_t v_in, uint32_t v_out, float r)
        : v_in_idx(v_in), v_out_idx(v_out), resistance(r) {
        (void)v_in; (void)v_out;
    }

    [[nodiscard]] std::string_view type_name() const override { return "Resistor"; }

    /// Propagate resistance upstream (for series circuit computation)
    void propagate_resistance(PushState& state) {
        // Get downstream resistance (what's connected to v_out)
        float r_downstream = state.signals[v_out_idx].resistance;

        // Total resistance seen at v_in = this resistor + downstream
        state.signals[v_in_idx].resistance = resistance + r_downstream;
    }

    /// Push voltage: V_out = V_in - I * R where I = V_in / R_total
    void push_voltage(PushState& state) {
        float v_in = state.signals[v_in_idx].voltage;

        // Total resistance (this + downstream)
        float r_total = resistance + state.signals[v_out_idx].resistance;

        if (r_total > 0.0f) {
            // I = V_in / R_total
            float i = v_in / r_total;

            // V_out = V_in - I * R = V_in - (V_in / R_total) * R
            state.signals[v_out_idx].voltage = v_in - (i * resistance);
        } else {
            // No downstream load - no voltage drop (open circuit)
            state.signals[v_out_idx].voltage = v_in;
        }
    }
};

/// Push-based Generator - voltage source that needs external excitation
class PushGenerator : public Component {
public:
    std::string name;
    uint32_t v_out_idx = 0;
    uint32_t rpm_idx = 0;

    float v_nominal = 28.0f;     // Nominal output voltage
    float rpm_threshold = 1000.0f;  // RPM needed to produce voltage
    float internal_r = 0.01f;    // Internal resistance

    PushGenerator() = default;
    PushGenerator(uint32_t v_out, uint32_t rpm)
        : v_out_idx(v_out), rpm_idx(rpm) {}

    [[nodiscard]] std::string_view type_name() const override { return "Generator"; }

    /// Push voltage: produces voltage only when RPM > threshold and load connected
    void push_voltage(PushState& state) {
        float rpm = state.signals[rpm_idx].voltage;
        float r_load = state.signals[v_out_idx].resistance;

        // Need RPM above threshold AND load connected to produce voltage
        if (rpm < rpm_threshold || r_load < 0.001f) {
            state.signals[v_out_idx].voltage = 0.0f;
            return;
        }

        // V_out = V_nominal - I * R_internal
        float i_load = v_nominal / (internal_r + r_load);
        float v_out = v_nominal - (i_load * internal_r);

        state.signals[v_out_idx].voltage = v_out;
    }
};

/// Push-based RefNode - reference voltage source (ground, power supply, etc.)
class PushRefNode : public Component {
public:
    std::string name;
    uint32_t v_idx = 0;
    float value = 0.0f;

    PushRefNode() = default;
    PushRefNode(uint32_t v, float val)
        : v_idx(v), value(val) {}

    [[nodiscard]] std::string_view type_name() const override { return "RefNode"; }

    /// Push voltage: outputs fixed reference voltage
    void push_voltage(PushState& state) {
        state.signals[v_idx].voltage = value;
    }
};

/// Push-based Wire - near-zero resistance conductor
class PushWire : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;

    float r_wire = 0.001f;  // 1mΩ (very low resistance)

    PushWire() = default;
    PushWire(uint32_t v_in, uint32_t v_out)
        : v_in_idx(v_in), v_out_idx(v_out) {}

    [[nodiscard]] std::string_view type_name() const override { return "Wire"; }

    /// Propagate resistance upstream
    void propagate_resistance(PushState& state) {
        float r_downstream = state.signals[v_out_idx].resistance;
        state.signals[v_in_idx].resistance = r_wire + r_downstream;
    }

    /// Push voltage: V_out ≈ V_in (negligible drop)
    void push_voltage(PushState& state) {
        float v_in = state.signals[v_in_idx].voltage;
        // Pass through with tiny voltage drop
        state.signals[v_out_idx].voltage = v_in * 0.9999f;  // 0.01% drop
    }
};

/// Push-based LerpNode - linear interpolation (first-order filter)
class PushLerpNode : public Component {
public:
    std::string name;
    uint32_t input_idx = 0;
    uint32_t output_idx = 0;

    float factor = 0.1f;  // Interpolation factor (0.0 to 1.0)
    float last_output = 0.0f;  // Previous output value (for smoothing)

    PushLerpNode() = default;
    PushLerpNode(uint32_t input, uint32_t output, float f = 0.1f)
        : input_idx(input), output_idx(output), factor(f) {}

    [[nodiscard]] std::string_view type_name() const override { return "LerpNode"; }

    /// Linear interpolation: output = output + (input - output) * factor
    /// This creates a first-order low-pass filter for sensor smoothing
    void push_voltage(PushState& state) {
        float input = state.signals[input_idx].voltage;

        // output += (input - output) * factor
        float delta = (input - last_output) * factor;
        last_output += delta;

        state.signals[output_idx].voltage = last_output;
    }
};

} // namespace an24
