#pragma once

#include "provider.h"
#include "../state.h"
#include <cstdint>
#include <string>

namespace an24 {

// =============================================================================
// Enums
// =============================================================================

/// GS24 operating modes
enum class GS24Mode { OFF, STARTER, STARTER_WAIT, GENERATOR };

/// APU (ВСУ) operating states - РУ19А-300
enum class APUState { OFF, CRANKING, IGNITION, RUNNING, STOPPING };

// =============================================================================
// Electrical Components - Template-based with Provider Pattern
// =============================================================================

/// Battery - voltage source with internal resistance
template <typename Provider = JitProvider>
class Battery {
public:
    Provider provider;
    std::string name;
    float capacity = 1000.0f;
    float charge = 1000.0f;
    float internal_r = 0.01f;
    float inv_internal_r = 100.0f;
    float v_nominal = 28.0f;

    Battery() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
    void pre_load();
};

/// Switch - manual toggle switch (triggered by control signal)
template <typename Provider = JitProvider>
class Switch {
public:
    Provider provider;
    bool closed = false;
    float last_control = 0.0f;
    float downstream_g = 0.0f;
    float downstream_I = 0.0f;
    float v_out_old = 0.0f;

    Switch() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
    void post_step(an24::SimulationState& st, float dt);
};

/// Relay - on/off switch controlled by voltage threshold
template <typename Provider = JitProvider>
class Relay {
public:
    Provider provider;
    bool closed = false;
    float hold_threshold = 0.5f;
    float downstream_g = 0.0f;
    float downstream_I = 0.0f;
    float v_out_old = 0.0f;

    Relay() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
    void post_step(an24::SimulationState& st, float dt);
};

/// HoldButton - hold-to-operate button with press/release detection
template <typename Provider = JitProvider>
class HoldButton {
public:
    Provider provider;
    float idle = 0.0f;
    float last_control = 0.0f;
    bool is_pressed = false;
    float downstream_g = 0.0f;
    float downstream_I = 0.0f;
    float v_out_old = 0.0f;

    HoldButton() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
    void post_step(an24::SimulationState& st, float dt);
};

/// Resistor - pure conductance element
template <typename Provider = JitProvider>
class Resistor {
public:
    Provider provider;
    float conductance = 0.1f;

    Resistor() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// Load - single port resistive load to ground
template <typename Provider = JitProvider>
class Load {
public:
    Provider provider;
    float conductance = 0.1f;

    Load() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// RefNode - fixed voltage reference
template <typename Provider = JitProvider>
class RefNode {
public:
    Provider provider;
    float value = 0.0f;

    RefNode() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// Bus - electrical bus/rail, connects all ports together
template <typename Provider = JitProvider>
class Bus {
public:
    Provider provider;

    Bus() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// Generator - voltage source like battery
template <typename Provider = JitProvider>
class Generator {
public:
    Provider provider;
    float internal_r = 0.005f;
    float v_nominal = 28.5f;

    Generator() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// GS24 - Starter-Generator
template <typename Provider = JitProvider>
class GS24 {
public:
    Provider provider;
    GS24Mode mode = GS24Mode::STARTER;
    float start_time = 0.0f;
    float wait_time = 0.0f;
    float r_internal = 0.025f;
    float k_motor = 0.5f;
    float i_max_starter = 800.0f;
    float rpm_cutoff = 0.45f;
    float v_nominal = 28.5f;
    float r_norton = 0.08f;
    float target_rpm = 15000.0f;
    float current_rpm = 0.0f;
    float i_max = 400.0f;
    float rpm_threshold = 0.4f;

    GS24() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
    void post_step(an24::SimulationState& st, float dt);
};

/// Transformer - AC transformer with voltage ratio
template <typename Provider = JitProvider>
class Transformer {
public:
    Provider provider;
    float ratio = 1.0f;

    Transformer() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// Inverter - DC to AC inverter
template <typename Provider = JitProvider>
class Inverter {
public:
    Provider provider;
    float efficiency = 0.95f;
    float frequency = 400.0f;

    Inverter() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// LerpNode - linear interpolation
template <typename Provider = JitProvider>
class LerpNode {
public:
    Provider provider;
    float factor = 1.0f;

    LerpNode() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
    void post_step(an24::SimulationState& st, float dt);
};

/// Splitter - 1-to-2 signal splitter
template <typename Provider = JitProvider>
class Splitter {
public:
    Provider provider;

    Splitter() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
    void solve_mechanical(an24::SimulationState& st, float dt);
    void solve_hydraulic(an24::SimulationState& st, float dt);
    void solve_thermal(an24::SimulationState& st, float dt);
};

/// IndicatorLight - aircraft indicator light
template <typename Provider = JitProvider>
class IndicatorLight {
public:
    Provider provider;
    float max_brightness = 100.0f;
    std::string color = "white";

    IndicatorLight() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// HighPowerLoad - high power electrical load
template <typename Provider = JitProvider>
class HighPowerLoad {
public:
    Provider provider;
    float power_draw = 500.0f;

    HighPowerLoad() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// Voltmeter - analog voltage gauge
template <typename Provider = JitProvider>
class Voltmeter {
public:
    Provider provider;

    Voltmeter() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// Gyroscope - power-only sensor
template <typename Provider = JitProvider>
class Gyroscope {
public:
    Provider provider;
    float conductance = 0.001f;

    Gyroscope() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// AGK47 - attitude gyro
template <typename Provider = JitProvider>
class AGK47 {
public:
    Provider provider;
    float conductance = 0.001f;

    AGK47() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

// =============================================================================
// Hydraulic Components
// =============================================================================

/// ElectricPump - electric motor driving hydraulic pump
template <typename Provider = JitProvider>
class ElectricPump {
public:
    Provider provider;
    float max_pressure = 1000.0f;

    ElectricPump() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
    void solve_hydraulic(an24::SimulationState& st, float dt);
};

/// SolenoidValve - electrically controlled hydraulic valve
template <typename Provider = JitProvider>
class SolenoidValve {
public:
    Provider provider;
    bool normally_closed = true;

    SolenoidValve() = default;

    void solve_hydraulic(an24::SimulationState& st, float dt);
};

// =============================================================================
// Mechanical Components
// =============================================================================

/// InertiaNode - mechanical inertia
template <typename Provider = JitProvider>
class InertiaNode {
public:
    Provider provider;
    float mass = 1.0f;
    float inv_mass = 1.0f;
    float damping = 0.5f;

    InertiaNode() = default;

    void solve_mechanical(an24::SimulationState& st, float dt);
    void pre_load();
};

// =============================================================================
// Thermal Components
// =============================================================================

/// TempSensor - temperature sensor
template <typename Provider = JitProvider>
class TempSensor {
public:
    Provider provider;
    float sensitivity = 1.0f;

    TempSensor() = default;

    void solve_thermal(an24::SimulationState& st, float dt);
};

/// ElectricHeater - electrical heater
template <typename Provider = JitProvider>
class ElectricHeater {
public:
    Provider provider;
    float max_power = 1000.0f;
    float efficiency = 0.9f;

    ElectricHeater() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
    void solve_thermal(an24::SimulationState& st, float dt);
};

/// RUG82 - Coal column voltage regulator
template <typename Provider = JitProvider>
class RUG82 {
public:
    Provider provider;
    float v_target = 28.5f;
    float k_mod = 0.5f;
    float kp = 2.0f;

    RUG82() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// DMR400 - Differential Minimum Relay
template <typename Provider = JitProvider>
class DMR400 {
public:
    Provider provider;
    bool is_closed = false;
    float connect_threshold = 2.0f;
    float disconnect_threshold = 10.0f;
    float min_voltage_to_close = 20.0f;
    float reconnect_delay = 0.0f;

    DMR400() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
    void post_step(an24::SimulationState& st, float dt);
};

/// RU19A-300 - Auxiliary Power Unit
template <typename Provider = JitProvider>
class RU19A {
public:
    Provider provider;
    APUState state = APUState::OFF;
    float timer = 0.0f;
    float target_rpm = 16000.0f;
    float current_rpm = 0.0f;
    float spinup_inertia = 1.0f;
    float spindown_inertia = 0.02f;
    float crank_time = 2.0f;
    float ignition_time = 3.0f;
    float runup_time = 8.0f;
    float start_timeout = 30.0f;
    float t4 = 0.0f;
    float t4_target = 400.0f;
    float t4_max = 750.0f;
    float ambient_temp = 20.0f;
    bool auto_start = true;

    RU19A() = default;

    void start() { if (state == APUState::OFF) state = APUState::CRANKING; }
    void stop() { state = APUState::STOPPING; }
    bool is_starter_active() const { return state == APUState::CRANKING || state == APUState::IGNITION; }

    void solve_electrical(an24::SimulationState& st, float dt);
    void solve_mechanical(an24::SimulationState& st, float dt);
    void solve_thermal(an24::SimulationState& st, float dt);
    void post_step(an24::SimulationState& st, float dt);
};

/// Radiator - heat exchanger
template <typename Provider = JitProvider>
class Radiator {
public:
    Provider provider;
    float cooling_capacity = 1000.0f;

    Radiator() = default;

    void solve_thermal(an24::SimulationState& st, float dt);
};

/// Comparator - voltage comparator with hysteresis
template <typename Provider = JitProvider>
class Comparator {
public:
    Provider provider;
    bool output_state = false;
    float Von = 5.0f;
    float Voff = 2.0f;

    Comparator() = default;

    void solve_logical(an24::SimulationState& st, float dt);
    void pre_load();
};

} // namespace an24
