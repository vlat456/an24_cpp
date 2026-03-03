#pragma once

#include "component.h"
#include <cstdint>
#include <string>

namespace an24 {

// =============================================================================
// Electrical Components
// =============================================================================

/// Battery - voltage source with internal resistance
class Battery : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    float capacity = 1000.0f;
    float charge = 1000.0f;
    float internal_r = 0.01f;
    float inv_internal_r = 100.0f;
    float v_nominal = 28.0f;

    Battery() = default;
    Battery(uint32_t v_in, uint32_t v_out, float v_nom, float int_r)
        : v_in_idx(v_in), v_out_idx(v_out), v_nominal(v_nom), internal_r(int_r) {}

    [[nodiscard]] std::string_view type_name() const override { return "Battery"; }
    void solve_electrical(SimulationState& state) override;
    void pre_load() override;
};

/// Relay - on/off switch (uses post_step to merge voltages when closed)
class Relay : public Component {
public:
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    bool closed = true;

    Relay() = default;
    Relay(uint32_t v_in, uint32_t v_out, bool is_closed = true)
        : v_in_idx(v_in), v_out_idx(v_out), closed(is_closed) {}

    [[nodiscard]] std::string_view type_name() const override { return "Relay"; }
    void solve_electrical(SimulationState& state) override;
    void post_step(SimulationState& state, float dt) override;
};

/// Resistor - pure conductance element (resistive load)
class Resistor : public Component {
public:
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    float conductance = 0.1f;

    Resistor(uint32_t v_in, uint32_t v_out, float g = 0.1f)
        : v_in_idx(v_in), v_out_idx(v_out), conductance(g) {}

    [[nodiscard]] std::string_view type_name() const override { return "Resistor"; }
    void solve_electrical(SimulationState& state) override;
};

/// Load - single port resistive load to ground
class Load : public Component {
public:
    uint32_t node_idx = 0;
    float conductance = 0.1f;  // draws I = V * g

    Load(uint32_t node, float g = 0.1f)
        : node_idx(node), conductance(g) {}

    [[nodiscard]] std::string_view type_name() const override { return "Load"; }
    void solve_electrical(SimulationState& state) override;
};

/// RefNode - fixed voltage reference (ground, bus)
/// Uses single "node" port - connects to the network as a fixed voltage
class RefNode : public Component {
public:
    uint32_t node_idx = 0;  // single port for the reference node
    float value = 0.0f;

    RefNode() = default;
    RefNode(uint32_t node, float val)
        : node_idx(node), value(val) {}

    [[nodiscard]] std::string_view type_name() const override { return "RefNode"; }
    void solve_electrical(SimulationState& state) override;
};

/// Bus - electrical bus/rail, connects all ports together
class Bus : public Component {
public:
    uint32_t bus_idx = 0;

    Bus() = default;
    explicit Bus(uint32_t idx) : bus_idx(idx) {}

    [[nodiscard]] std::string_view type_name() const override { return "Bus"; }
    void solve_electrical(SimulationState& state) override {
        // Bus is just a wire - no component behavior
    }
};

/// Generator - voltage source like battery
class Generator : public Component {
public:
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    float internal_r = 0.005f;
    float v_nominal = 28.5f;

    Generator() = default;
    Generator(uint32_t v_in, uint32_t v_out, float v_nom, float int_r)
        : v_in_idx(v_in), v_out_idx(v_out), v_nominal(v_nom), internal_r(int_r) {}

    [[nodiscard]] std::string_view type_name() const override { return "Generator"; }
    void solve_electrical(SimulationState& state) override;
};

/// Transformer - AC transformer with voltage ratio
class Transformer : public Component {
public:
    uint32_t primary_idx = 0;
    uint32_t secondary_idx = 0;
    float ratio = 1.0f;  // primary / secondary

    Transformer(uint32_t primary, uint32_t secondary, float r)
        : primary_idx(primary), secondary_idx(secondary), ratio(r) {}

    [[nodiscard]] std::string_view type_name() const override { return "Transformer"; }
    void solve_electrical(SimulationState& state) override;
};

/// Inverter - DC to AC inverter
class Inverter : public Component {
public:
    uint32_t dc_in_idx = 0;
    uint32_t ac_out_idx = 0;
    float efficiency = 0.95f;
    float frequency = 400.0f;

    Inverter(uint32_t dc_in, uint32_t ac_out, float eff, float freq)
        : dc_in_idx(dc_in), ac_out_idx(ac_out), efficiency(eff), frequency(freq) {}

    [[nodiscard]] std::string_view type_name() const override { return "Inverter"; }
    void solve_electrical(SimulationState& state) override;
};

/// LerpNode - linear interpolation (voltage display filter)
class LerpNode : public Component {
public:
    uint32_t input_idx = 0;
    uint32_t output_idx = 0;
    float factor = 1.0f;

    LerpNode(uint32_t input, uint32_t output, float f)
        : input_idx(input), output_idx(output), factor(f) {}

    [[nodiscard]] std::string_view type_name() const override { return "LerpNode"; }
    void solve_electrical(SimulationState& state) override;
};

/// IndicatorLight - aircraft indicator light (two power terminals + brightness output)
class IndicatorLight : public Component {
public:
    uint32_t v_in_idx = 0;        // power input (bus side)
    uint32_t v_out_idx = 0;       // power return (ground side)
    uint32_t brightness_idx = 0;  // brightness output signal
    float max_brightness = 100.0f;
    std::string color = "white";

    IndicatorLight(uint32_t v_in, uint32_t v_out, uint32_t brightness, float max_bright, std::string c)
        : v_in_idx(v_in), v_out_idx(v_out), brightness_idx(brightness), max_brightness(max_bright), color(std::move(c)) {}

    [[nodiscard]] std::string_view type_name() const override { return "IndicatorLight"; }
    void solve_electrical(SimulationState& state) override;
};

/// HighPowerLoad - high power electrical load
class HighPowerLoad : public Component {
public:
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    float power_draw = 500.0f;

    HighPowerLoad(uint32_t v_in, uint32_t v_out, float power)
        : v_in_idx(v_in), v_out_idx(v_out), power_draw(power) {}

    [[nodiscard]] std::string_view type_name() const override { return "HighPowerLoad"; }
    void solve_electrical(SimulationState& state) override;
};

// =============================================================================
// Sensors (Electrical input, no output)
// =============================================================================

/// Gyroscope - power-only sensor
class Gyroscope : public Component {
public:
    uint32_t input_idx = 0;

    Gyroscope() = default;
    explicit Gyroscope(uint32_t input) : input_idx(input) {}

    [[nodiscard]] std::string_view type_name() const override { return "Gyroscope"; }
    void solve_electrical(SimulationState& state) override;
};

/// AGK47 - attitude gyro
class AGK47 : public Component {
public:
    uint32_t input_idx = 0;

    AGK47() = default;
    explicit AGK47(uint32_t input) : input_idx(input) {}

    [[nodiscard]] std::string_view type_name() const override { return "AGK47"; }
    void solve_electrical(SimulationState& state) override;
};

// =============================================================================
// Hydraulic Components (multi-domain)
// =============================================================================

/// ElectricPump - electric motor driving hydraulic pump
class ElectricPump : public Component {
public:
    uint32_t v_in_idx = 0;
    uint32_t p_out_idx = 0;
    float max_pressure = 1000.0f;

    ElectricPump(uint32_t v_in, uint32_t p_out, float max_p)
        : v_in_idx(v_in), p_out_idx(p_out), max_pressure(max_p) {}

    [[nodiscard]] std::string_view type_name() const override { return "ElectricPump"; }
    void solve_electrical(SimulationState& state) override;
    void solve_hydraulic(SimulationState& state) override;
};

/// SolenoidValve - electrically controlled hydraulic valve
class SolenoidValve : public Component {
public:
    uint32_t ctrl_idx = 0;
    uint32_t flow_in_idx = 0;
    uint32_t flow_out_idx = 0;
    bool normally_closed = true;

    SolenoidValve(uint32_t ctrl, uint32_t flow_in, uint32_t flow_out, bool nc)
        : ctrl_idx(ctrl), flow_in_idx(flow_in), flow_out_idx(flow_out), normally_closed(nc) {}

    [[nodiscard]] std::string_view type_name() const override { return "SolenoidValve"; }
    void solve_hydraulic(SimulationState& state) override;
};

// =============================================================================
// Mechanical Components
// =============================================================================

/// InertiaNode - mechanical inertia (first-order lag)
class InertiaNode : public Component {
public:
    uint32_t input_idx = 0;
    uint32_t output_idx = 0;
    float mass = 1.0f;
    float inv_mass = 1.0f;
    float damping = 0.5f;

    InertiaNode(uint32_t input, uint32_t output, float m, float d)
        : input_idx(input), output_idx(output), mass(m), damping(d) {
        inv_mass = (m > 0.0f) ? 1.0f / m : 0.0f;
    }

    [[nodiscard]] std::string_view type_name() const override { return "InertiaNode"; }
    void solve_mechanical(SimulationState& state) override;
    void pre_load() override;
};

// =============================================================================
// Thermal Components
// =============================================================================

/// TempSensor - temperature sensor
class TempSensor : public Component {
public:
    uint32_t temp_in_idx = 0;
    uint32_t temp_out_idx = 0;
    float sensitivity = 1.0f;

    TempSensor(uint32_t temp_in, uint32_t temp_out, float sens)
        : temp_in_idx(temp_in), temp_out_idx(temp_out), sensitivity(sens) {}

    [[nodiscard]] std::string_view type_name() const override { return "TempSensor"; }
    void solve_thermal(SimulationState& state) override;
};

/// ElectricHeater - electrical heater
class ElectricHeater : public Component {
public:
    uint32_t power_idx = 0;
    uint32_t heat_out_idx = 0;
    float max_power = 1000.0f;
    float efficiency = 0.9f;

    ElectricHeater(uint32_t power, uint32_t heat_out, float max_p, float eff)
        : power_idx(power), heat_out_idx(heat_out), max_power(max_p), efficiency(eff) {}

    [[nodiscard]] std::string_view type_name() const override { return "ElectricHeater"; }
    void solve_electrical(SimulationState& state) override;
    void solve_thermal(SimulationState& state) override;
};

/// Radiator - heat exchanger
class Radiator : public Component {
public:
    uint32_t heat_in_idx = 0;
    uint32_t heat_out_idx = 0;
    float cooling_capacity = 1000.0f;

    Radiator(uint32_t heat_in, uint32_t heat_out, float capacity)
        : heat_in_idx(heat_in), heat_out_idx(heat_out), cooling_capacity(capacity) {}

    [[nodiscard]] std::string_view type_name() const override { return "Radiator"; }
    void solve_thermal(SimulationState& state) override;
};

} // namespace an24
