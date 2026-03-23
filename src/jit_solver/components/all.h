#pragma once

#include "provider.h"
#include "../state.h"
#include <cstdint>
#include <string>

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
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    std::string name;
    float capacity = 1000.0f;
    float charge = 1000.0f;
    float internal_r = 0.01f;
    float inv_internal_r = 100.0f;
    float v_nominal = 28.0f;

    Battery() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load();
};

/// Switch - manual toggle switch (triggered by control signal)
template <typename Provider = JitProvider>
class Switch {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    bool closed = false;
    float last_control = 0.0f;
    float downstream_g = 0.0f;
    float downstream_I = 0.0f;
    float v_out_old = 0.0f;

    Switch() = default;

    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load() {}
};

/// Relay - on/off switch controlled by voltage threshold
template <typename Provider = JitProvider>
class Relay {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    bool closed = false;
    float hold_threshold = 0.5f;
    float downstream_g = 0.0f;
    float downstream_I = 0.0f;
    float v_out_old = 0.0f;

    Relay() = default;

    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load() {}
};

/// HoldButton - hold-to-operate button with press/release detection
template <typename Provider = JitProvider>
class HoldButton {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float idle = 0.0f;
    float last_control = 0.0f;
    bool is_pressed = false;
    float downstream_g = 0.0f;
    float downstream_I = 0.0f;
    float v_out_old = 0.0f;

    HoldButton() = default;

    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load() {}
};

/// Resistor - pure conductance element
template <typename Provider = JitProvider>
class Resistor {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float conductance = 0.1f;

    Resistor() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Load - single port resistive load to ground
template <typename Provider = JitProvider>
class Load {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float conductance = 0.1f;

    Load() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// RefNode - fixed voltage reference
template <typename Provider = JitProvider>
class RefNode {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float value = 0.0f;

    RefNode() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Bus - electrical bus/rail, connects all ports together
template <typename Provider = JitProvider>
class Bus {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;

    Bus() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// BlueprintInput - input port marker for nested blueprints
template <typename Provider = JitProvider>
class BlueprintInput {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;

    // Exposed port metadata (for parent blueprint)
    std::string exposed_type_str = "V";      // For type validation
    std::string exposed_direction_str = "In";  // For direction validation

    BlueprintInput() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// BlueprintOutput - output port marker for nested blueprints
template <typename Provider = JitProvider>
class BlueprintOutput {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;

    // Exposed port metadata (for parent blueprint)
    std::string exposed_type_str = "V";      // For type validation
    std::string exposed_direction_str = "Out";  // For direction validation

    BlueprintOutput() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Generator - voltage source like battery
template <typename Provider = JitProvider>
class Generator {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float internal_r = 0.005f;
    float inv_internal_r = 200.0f; // Precomputed
    float v_nominal = 28.5f;

    Generator() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load();
};

/// GS24 - Starter-Generator
template <typename Provider = JitProvider>
class GS24 {
public:
    static constexpr Domain domain = Domain::Electrical;

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

    // Precomputed inverses (set in pre_load)
    float inv_r_internal = 40.0f;   // 1/r_internal
    float inv_r_norton = 12.5f;     // 1/r_norton
    float inv_target_rpm = 1.0f / 15000.0f;

    GS24() = default;

    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load();
};

/// Transformer - AC transformer with voltage ratio
template <typename Provider = JitProvider>
class Transformer {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float ratio = 1.0f;

    Transformer() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Inverter - DC to AC inverter
template <typename Provider = JitProvider>
class Inverter {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float efficiency = 0.95f;
    float frequency = 400.0f;

    Inverter() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// LerpNode - linear interpolation with deadzone
template <typename Provider = JitProvider>
class LerpNode {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float factor = 1.0f;
    float deadzone = 0.001f;
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    LerpNode() = default;

    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load() {}
};

/// PID - Proportional-Integral-Derivative controller
template <typename Provider = JitProvider>
class PID {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float Kp = 1.0f;
    float Ki = 0.0f;
    float Kd = 0.0f;
    float output_min = -1000.0f;
    float output_max = 1000.0f;
    float filter_alpha = 0.2f;

    // State variables (minimal footprint: 3 floats)
    float integral = 0.0f;
    float last_error = 0.0f;
    float d_filtered = 0.0f;

    PID() = default;

    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load() {}
};

/// PD - Proportional-Derivative controller
template <typename Provider = JitProvider>
class PD {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float Kp = 1.0f;
    float Kd = 0.0f;
    float output_min = -1000.0f;
    float output_max = 1000.0f;
    float filter_alpha = 0.2f;

    // State variables (minimal footprint: 2 floats, no integral)
    float last_error = 0.0f;
    float d_filtered = 0.0f;

    PD() = default;

    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load() {}
};

/// PI - Proportional-Integral controller
template <typename Provider = JitProvider>
class PI {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float Kp = 1.0f;
    float Ki = 0.0f;
    float output_min = -1000.0f;
    float output_max = 1000.0f;

    // State variables (minimal footprint: 1 float, no derivative)
    float integral = 0.0f;

    PI() = default;

    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load() {}
};

/// P - Proportional controller
template <typename Provider = JitProvider>
class P {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float Kp = 1.0f;
    float output_min = -1000.0f;
    float output_max = 1000.0f;

    // No state variables (pure memoryless function)

    P() = default;

    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load() {}
};

/// Splitter - 1-to-2 signal splitter
template <typename Provider = JitProvider>
class Splitter {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;

    Splitter() = default;

    void solve_electrical(SimulationState& st, float dt);
    void solve_mechanical(SimulationState& st, float dt);
    void solve_hydraulic(SimulationState& st, float dt);
    void solve_thermal(SimulationState& st, float dt);
    void pre_load() {}
};

/// Merger - 2-to-1 signal merger (inverse of Splitter)
template <typename Provider = JitProvider>
class Merger {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;

    Merger() = default;

    void solve_electrical(SimulationState& st, float dt);
    void solve_mechanical(SimulationState& st, float dt);
    void solve_hydraulic(SimulationState& st, float dt);
    void solve_thermal(SimulationState& st, float dt);
    void pre_load() {}
};

/// CurrentSense - series current measurement node (ammeter)
/// Sits in series between v_in and v_out with near-zero resistance.
/// Outputs measured current (amperes) on i_out port.
template <typename Provider = JitProvider>
class CurrentSense {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float conductance = 1000.0f;  // high conductance = low series resistance

    CurrentSense() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// IndicatorLight - aircraft indicator light
template <typename Provider = JitProvider>
class IndicatorLight {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float max_brightness = 100.0f;
    float conductance = 1.0f;  // low resistance pass-through indicator
    float rated_voltage = 28.0f;
    float inv_rated_voltage = 1.0f / 28.0f; // precomputed
    std::string color = "white";

    IndicatorLight() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load();
};

/// HighPowerLoad - high power electrical load (branchless, optimized)
template <typename Provider = JitProvider>
class HighPowerLoad {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float power_draw = 500.0f;
    float min_voltage_diff = 0.01f; // Minimum voltage diff to conduct

    HighPowerLoad() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Voltmeter - analog voltage gauge
template <typename Provider = JitProvider>
class Voltmeter {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float min = 0.0f;    // Gauge minimum display value
    float max = 28.0f;   // Gauge maximum display value

    Voltmeter() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

// =============================================================================
// Domain Bridge Components - Electrical <-> Control signal crossing
// =============================================================================

/// VoltageSense - electrical-to-control bridge (observer only)
/// Reads (v_in - v_ref) after the electrical solve, outputs a scalar control signal.
/// Does NOT stamp conductance - purely observes, never drives the network.
/// Domain: Electrical (to read ports after SOR) + Logical (to write output)
template <typename Provider = JitProvider>
class VoltageSense {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Logical;

    Provider provider;
    float gain   = 1.0f;
    float offset = 0.0f;

    VoltageSense() = default;

    void solve_electrical(SimulationState& st, float dt);  // no-op stamp, output written here
    void solve_logical(SimulationState& st, float dt);     // writes out = (v_in - v_ref) * gain + offset
    void pre_load() {}
};

/// ControlledVoltageSource - control-to-electrical bridge (Thevenin source)
/// Reads cmd (control scalar), stamps a Norton-equivalent voltage source each frame.
/// v_source = clamp(cmd * gain + offset, min_v, max_v)
template <typename Provider = JitProvider>
class ControlledVoltageSource {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float gain       = 1.0f;
    float offset     = 0.0f;
    float min_v      = 0.0f;
    float max_v      = 30.0f;
    float r_internal = 0.1f;
    float inv_r      = 10.0f;  // precomputed

    ControlledVoltageSource() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load();
};

/// ControlledCurrentSource - control-to-electrical bridge (Norton current source)
/// Reads cmd (control scalar), stamps commanded current into the network.
/// i_source = clamp(cmd * gain, min_i, max_i)
template <typename Provider = JitProvider>
class ControlledCurrentSource {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float gain    = 1.0f;
    float min_i   = 0.0f;
    float max_i   = 100.0f;
    float g_shunt = 0.001f;  // small parallel conductance for well-conditioning

    ControlledCurrentSource() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// VariableConductance - control-to-electrical bridge (variable resistor)
/// Reads cmd [0..1], stamps g = lerp(g_min, g_max, cmd) between v_in and v_out.
/// Models command-controlled winding resistance, field effect, or variable load.
template <typename Provider = JitProvider>
class VariableConductance {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float g_min = 0.001f;
    float g_max = 10.0f;

    VariableConductance() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Gyroscope - power-only sensor
template <typename Provider = JitProvider>
class Gyroscope {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float conductance = 0.001f;

    Gyroscope() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

/// AGK47 - attitude gyro
template <typename Provider = JitProvider>
class AGK47 {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float conductance = 0.001f;

    AGK47() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};

// =============================================================================
// Hydraulic Components
// =============================================================================

/// ElectricPump - electric motor driving hydraulic pump
/// Now a two-port hydraulic component (p_in → p_out) that can form closed loops.
/// Pressure boost is proportional to electrical supply voltage.
/// Electrical draw is load-dependent (proportional to hydraulic flow × pressure).
template <typename Provider = JitProvider>
class ElectricPump {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Hydraulic;

    Provider provider;
    float max_pressure = 1000.0f;

    ElectricPump() = default;

    void solve_electrical(SimulationState& st, float dt);
    void solve_hydraulic(SimulationState& st, float dt);
    void pre_load() {}
};

/// SolenoidValve - electrically controlled hydraulic valve (branchless)
template <typename Provider = JitProvider>
class SolenoidValve {
public:
    static constexpr Domain domain = Domain::Hydraulic;

    Provider provider;
    bool normally_closed = true;
    float open_mask = 0.0f; // Branchless state (0.0 = closed, 1.0 = open)

    SolenoidValve() = default;

    void solve_hydraulic(SimulationState& st, float dt);
    void pre_load() {}
};

/// GidroAccumulator - gas-charged hydraulic accumulator (Boyle's law)
/// Stores hydraulic energy via compressed gas. When system pressure exceeds
/// precharge, fluid enters and gas compresses. When pressure drops, stored
/// fluid is released back into the circuit.
template <typename Provider = JitProvider>
class GidroAccumulator {
public:
    static constexpr Domain domain = Domain::Hydraulic;

    Provider provider;
    float precharge_pressure = 50.0f;  // Gas precharge pressure (psi)
    float volume = 10.0f;              // Total accumulator volume (liters)
    float gas_volume = 10.0f;          // Current gas volume (liters, state)

    GidroAccumulator() = default;

    void solve_hydraulic(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load();
};

/// FuelTank - aircraft fuel reservoir with gravity head pressure
/// Provides fuel flow proportional to fuel level. Level decreases
/// as flow is consumed by downstream components.
template <typename Provider = JitProvider>
class FuelTank {
public:
    static constexpr Domain domain = Domain::Hydraulic;

    Provider provider;
    float capacity = 1000.0f;   // Tank capacity (liters)
    float level = 1000.0f;      // Current fuel amount (liters, state)
    float density = 0.78f;      // Fuel density (kg/L, kerosene TS-1)
    float inv_capacity = 0.001f; // Precomputed 1/capacity

    FuelTank() = default;

    void solve_hydraulic(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load();
};

// =============================================================================
// Mechanical Components
// =============================================================================

/// InertiaNode - mechanical inertia
template <typename Provider = JitProvider>
class InertiaNode {
public:
    static constexpr Domain domain = Domain::Mechanical;

    Provider provider;
    float mass = 1.0f;
    float inv_mass = 1.0f;
    float damping = 0.5f;

    InertiaNode() = default;

    void solve_mechanical(SimulationState& st, float dt);
    void pre_load();
};

/// Spring - mechanical spring-damper with preload
template <typename Provider = JitProvider>
class Spring {
public:
    static constexpr Domain domain = Domain::Mechanical;
    Provider provider;

    float k = 1000.0f;          // Stiffness (N/m)
    float c = 10.0f;            // Viscous damping coefficient (N*s/m)
    float rest_length = 0.1f;   // Free length
    bool compression_only = true;
    float compression_only_f = 1.0f; // precomputed: 1.0 = compression_only, 0.0 = both

    // State for velocity estimation (finite difference)
    float prev_delta_x = 0.0f;
    float first_frame_mask = 1.0f; // Branchless cold start

    Spring() = default;

    void solve_mechanical(SimulationState& st, float dt);
    void pre_load();
};

// =============================================================================
// Thermal Components
// =============================================================================

/// TempSensor - temperature sensor
template <typename Provider = JitProvider>
class TempSensor {
public:
    static constexpr Domain domain = Domain::Thermal;

    Provider provider;
    float sensitivity = 1.0f;

    TempSensor() = default;

    void solve_thermal(SimulationState& st, float dt);
    void pre_load() {}
};

/// ElectricHeater - electrical heater
template <typename Provider = JitProvider>
class ElectricHeater {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Thermal;

    Provider provider;
    float max_power = 1000.0f;
    float efficiency = 0.9f;

    ElectricHeater() = default;

    void solve_electrical(SimulationState& st, float dt);
    void solve_thermal(SimulationState& st, float dt);
    void pre_load() {}
};

/// RUG82 - Coal column voltage regulator
template <typename Provider = JitProvider>
class RUG82 {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float v_target = 28.5f;
    float k_mod = 0.5f;
    float kp = 2.0f;

    RUG82() = default;

    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load() {}
};

/// DMR400 - Differential Minimum Relay
template <typename Provider = JitProvider>
class DMR400 {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    bool is_closed = false;
    float connect_threshold = 2.0f;
    float disconnect_threshold = 10.0f;
    float min_voltage_to_close = 20.0f;
    float reconnect_delay = 0.0f;

    DMR400() = default;

    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load() {}
};

/// RU19A-300 - Auxiliary Power Unit
template <typename Provider = JitProvider>
class RU19A {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Mechanical | Domain::Thermal;

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

    // Precomputed inverses (set in pre_load)
    float inv_target_rpm = 1.0f / 16000.0f;

    RU19A() = default;

    void start() { if (state == APUState::OFF) state = APUState::CRANKING; }
    void stop() { state = APUState::STOPPING; }
    bool is_starter_active() const { return state == APUState::CRANKING || state == APUState::IGNITION; }

    void solve_electrical(SimulationState& st, float dt);
    void solve_mechanical(SimulationState& st, float dt);
    void solve_thermal(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load();
};

/// Radiator - heat exchanger
template <typename Provider = JitProvider>
class Radiator {
public:
    static constexpr Domain domain = Domain::Thermal;

    Provider provider;
    float cooling_capacity = 1000.0f;

    Radiator() = default;

    void solve_thermal(SimulationState& st, float dt);
    void pre_load() {}
};

/// AZS (Автомат Защиты Сети) — Aircraft circuit breaker
/// Hybrid switch + thermal fuse. Manual toggle via control port.
/// Thermal model: T += (I² * r_heat - T * k_cool) * dt, trips at T > 1.0
template <typename Provider = JitProvider>
class AZS {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Thermal;

    Provider provider;
    bool closed = false;
    bool tripped = false;
    float last_control = 0.0f;
    float downstream_g = 0.0f;
    float downstream_I = 0.0f;
    float v_out_old = 0.0f;
    float temp = 0.0f;
    float current = 0.0f;
    float i_nominal = 20.0f;
    float r_heat = 0.0025f;   // 1/(i_nominal²) — precomputed
    float k_cool = 1.0f;

    AZS() = default;

    void solve_electrical(SimulationState& st, float dt);
    void solve_thermal(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load();
};

/// Comparator - voltage comparator with hysteresis
template <typename Provider = JitProvider>
class Comparator {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    bool output_state = false;
    float Von = 5.0f;
    float Voff = 2.0f;

    Comparator() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load();
};

/// Subtract - subtractor (o = A - B)
template <typename Provider = JitProvider>
class Subtract {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Subtract() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Multiply - multiplier (o = A * B)
template <typename Provider = JitProvider>
class Multiply {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Multiply() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Divide - divider (o = A / B, returns 0 if B is 0)
template <typename Provider = JitProvider>
class Divide {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Divide() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Add - adder (o = A + B)
template <typename Provider = JitProvider>
class Add {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Add() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// AND - logical AND gate (o = A && B)
template <typename Provider = JitProvider>
class AND {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    AND() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// OR - logical OR gate (o = A || B)
template <typename Provider = JitProvider>
class OR {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    OR() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// XOR - logical XOR gate (o = A != B)
template <typename Provider = JitProvider>
class XOR {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    XOR() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// NOT - logical NOT gate (o = !A)
template <typename Provider = JitProvider>
class NOT {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    NOT() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// NAND - logical NAND gate (o = !(A && B))
template <typename Provider = JitProvider>
class NAND {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    NAND() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Any_V_to_Bool - convert any non-zero voltage to TRUE (including negative)
template <typename Provider = JitProvider>
class Any_V_to_Bool {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Any_V_to_Bool() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Positive_V_to_Bool - convert positive voltage to TRUE (v > 0)
template <typename Provider = JitProvider>
class Positive_V_to_Bool {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Positive_V_to_Bool() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// LUT - Lookup table with linear interpolation.
/// Table data lives in SimulationState arena (cache-friendly, contiguous).
/// Component holds only offset+size into the shared arena.
template <typename Provider = JitProvider>
class LUT {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    uint32_t table_offset = 0;  ///< Index into st.lut_keys / st.lut_values
    uint16_t table_size   = 0;  ///< Number of breakpoints

    LUT() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}

    /// Parse "k1:v1; k2:v2; ..." table string into keys/values vectors
    static bool parse_table(const std::string& table_str,
                            std::vector<float>& keys,
                            std::vector<float>& values);

private:
    static float interpolate(float x, const float* keys, const float* vals, uint16_t size);
};

/// FastTMO - fast generalized Time Management Offset filter (low-pass)
template <typename Provider = JitProvider>
class FastTMO {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float tau = 0.1f;
    float inv_tau = 10.0f; // Precomputed
    float deadzone = 0.001f;
    float current_value = 0.0f;
    float first_frame_mask = 1.0f; // Branchless init mask

    FastTMO() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load();
};

/// AsymTMO - asymmetric Time Management Offset filter (different rise/fall rates)
template <typename Provider = JitProvider>
class AsymTMO {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float tau_up = 0.1f;
    float tau_down = 0.5f;
    float inv_tau_up = 10.0f;
    float inv_tau_down = 2.0f;
    float deadzone = 0.001f;
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    AsymTMO() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load();
};

/// SlewRate - linear rate of change limiter (slew rate limiter)
template <typename Provider = JitProvider>
class SlewRate {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float max_rate = 1.0f;
    float deadzone = 0.0001f;
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    SlewRate() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// AsymSlewRate - asymmetric linear rate limiter (different rise/fall rates)
template <typename Provider = JitProvider>
class AsymSlewRate {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float rate_up = 1.0f;
    float rate_down = 0.5f;
    float deadzone = 0.0001f;
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    AsymSlewRate() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// TimeDelay - logic delay node with separate ON and OFF timers
template <typename Provider = JitProvider>
class TimeDelay {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float delay_on = 0.5f;
    float delay_off = 0.1f;

    float accumulator = 0.0f;
    float current_out = 0.0f;
    float last_in = 0.0f;
    float first_frame_mask = 1.0f;

    TimeDelay() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Monostable - pulse timer (one-shot): outputs 1.0 for duration after rising edge
template <typename Provider = JitProvider>
class Monostable {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float duration = 30.0f;
    float timer = 0.0f;
    float last_in = 0.0f;

    Monostable() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// SampleHold - samples input on trigger rising edge and holds value
template <typename Provider = JitProvider>
class SampleHold {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float stored_value = 0.0f;
    float last_trig = 0.0f;

    SampleHold() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Integrator - mathematical integrator with reset: out = integral(in * dt)
template <typename Provider = JitProvider>
class Integrator {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float gain = 1.0f;
    float initial_val = 0.0f;
    float accumulator = 0.0f;
    float first_frame_mask = 1.0f;

    Integrator() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Clamp - clamps input value between min and max
template <typename Provider = JitProvider>
class Clamp {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float min = 0.0f;
    float max = 1.0f;

    Clamp() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Normalize - maps [min..max] range to [0..1], result clamped
template <typename Provider = JitProvider>
class Normalize {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float min = 0.0f;
    float max = 100.0f;
    float inv_range = 0.01f;  // precomputed in pre_load()

    Normalize() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load();
};

/// Min - outputs the smaller of two inputs
template <typename Provider = JitProvider>
class Min {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Min() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Max - outputs the larger of two inputs
template <typename Provider = JitProvider>
class Max {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Max() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Greater - outputs 1.0 if A > B, else 0.0
template <typename Provider = JitProvider>
class Greater {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Greater() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Lesser - outputs 1.0 if A < B, else 0.0
template <typename Provider = JitProvider>
class Lesser {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Lesser() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// GreaterEq - outputs 1.0 if A >= B, else 0.0
template <typename Provider = JitProvider>
class GreaterEq {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    GreaterEq() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// LesserEq - outputs 1.0 if A <= B, else 0.0
template <typename Provider = JitProvider>
class LesserEq {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    LesserEq() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};

/// Slider - interactive value source (control -> out passthrough)
template <typename Provider = JitProvider>
class Slider {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;
    float min = 0.0f;
    float max = 1.0f;

    Slider() = default;

    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};
