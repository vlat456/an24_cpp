#pragma once

#include "../component.h"
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
// Electrical Components
// =============================================================================

/// Battery - voltage source with internal resistance
class Battery final : public Component {
public:
    std::string name;
    PORTS(Battery, v_in, v_out)
    float capacity = 1000.0f;
    float charge = 1000.0f;
    float internal_r = 0.01f;
    float inv_internal_r = 100.0f;
    float v_nominal = 28.0f;

    Battery() = default;
    Battery(uint32_t v_in, uint32_t v_out, float v_nom, float int_r)
        : v_in_idx(v_in), v_out_idx(v_out), v_nominal(v_nom), internal_r(int_r) {}

    [[nodiscard]] std::string_view type_name() const override { return "Battery"; }
    void solve_electrical(SimulationState& state, float dt) override;
    void pre_load() override;
};

/// Switch - manual toggle switch (triggered by control signal)
/// Switch - manual toggle switch (triggered by control signal)
/// Mirrors downstream conductance onto v_in so battery sees the load.
/// post_step forces v_out=v_in when closed, v_out=0 when open.
class Switch final : public Component {
public:
    PORTS(Switch, control, state, v_in, v_out)
    bool closed = false;        // Initial state (default: open)
    float last_control = 0.0f;  // Previous control voltage (edge detection)
    float downstream_g = 0.0f;  // Cached downstream conductance (from prev step)
    float downstream_I = 0.0f;  // Cached downstream Norton current (from prev step)
    float v_out_old = 0.0f;     // V_out at start of step (before SOR)

    Switch() = default;
    Switch(uint32_t v_in, uint32_t v_out, uint32_t control, uint32_t state, bool is_closed = false)
        : v_in_idx(v_in), v_out_idx(v_out), control_idx(control), state_idx(state), closed(is_closed), last_control(0.0f) {}

    [[nodiscard]] std::string_view type_name() const override { return "Switch"; }
    void solve_electrical(SimulationState& state, float dt) override;
    void post_step(SimulationState& state, float dt) override;
};

/// Relay - on/off switch controlled by voltage threshold.
/// Mirrors downstream conductance onto v_in so battery sees the load.
class Relay final : public Component {
public:
    PORTS(Relay, control, v_in, v_out)
    bool closed = false;        // Current state
    float hold_threshold = 0.5f;  // Voltage threshold to hold closed
    float downstream_g = 0.0f;  // Cached downstream conductance (from prev step)
    float downstream_I = 0.0f;  // Cached downstream Norton current (from prev step)
    float v_out_old = 0.0f;     // V_out at start of step (before SOR)

    Relay() = default;
    Relay(uint32_t v_in, uint32_t v_out, uint32_t control, bool is_closed = false, float threshold = 0.5f)
        : v_in_idx(v_in), v_out_idx(v_out), control_idx(control), closed(is_closed), hold_threshold(threshold) {}

    [[nodiscard]] std::string_view type_name() const override { return "Relay"; }
    void solve_electrical(SimulationState& state, float dt) override;
    void post_step(SimulationState& state, float dt) override;
};

/// HoldButton - hold-to-operate button with press/release detection.
/// Mirrors downstream conductance onto v_in so battery sees the load.
/// Control Protocol: 0.0V=Idle, 1.0V=Pressed, 2.0V=Released
/// State output: 1.0V = pressed, 0.0V = released/idle
class HoldButton final : public Component {
public:
    PORTS(HoldButton, control, state, v_in, v_out)
    float idle = 0.0f;          // Idle value (when button not pressed)
    float last_control = 0.0f;   // Previous control value (edge detection)
    bool is_pressed = false;     // Current button state (latched)
    float downstream_g = 0.0f;  // Cached downstream conductance (from prev step)
    float downstream_I = 0.0f;  // Cached downstream Norton current (from prev step)
    float v_out_old = 0.0f;     // V_out at start of step (before SOR)

    HoldButton() = default;
    HoldButton(uint32_t v_in, uint32_t v_out, uint32_t control, uint32_t state, float idle_val = 0.0f)
        : v_in_idx(v_in), v_out_idx(v_out), control_idx(control), state_idx(state), idle(idle_val) {}

    [[nodiscard]] std::string_view type_name() const override { return "HoldButton"; }
    void solve_electrical(SimulationState& state, float dt) override;
    void post_step(SimulationState& state, float dt) override;
};

/// Resistor - pure conductance element (resistive load)
class Resistor final : public Component {
public:
    PORTS(Resistor, v_in, v_out)
    float conductance = 0.1f;

    Resistor(uint32_t v_in, uint32_t v_out, float g = 0.1f)
        : v_in_idx(v_in), v_out_idx(v_out), conductance(g) {}

    [[nodiscard]] std::string_view type_name() const override { return "Resistor"; }
    void solve_electrical(SimulationState& state, float dt) override;
};

/// Load - single port resistive load to ground
class Load final : public Component {
public:
    PORTS(Load, input)
    float conductance = 0.1f;  // draws I = V * g

    Load() = default;
    Load(uint32_t input, float g = 0.1f)
        : input_idx(input), conductance(g) {}

    [[nodiscard]] std::string_view type_name() const override { return "Load"; }
    void solve_electrical(SimulationState& state, float dt) override;
};

/// RefNode - fixed voltage reference (ground, bus)
/// Uses single "v" port - connects to the network as a fixed voltage
class RefNode final : public Component {
public:
    PORTS(RefNode, v)
    float value = 0.0f;

    RefNode() = default;
    RefNode(uint32_t v, float val)
        : v_idx(v), value(val) {}

    [[nodiscard]] std::string_view type_name() const override { return "RefNode"; }
    void solve_electrical(SimulationState& state, float dt) override;
};

/// Bus - electrical bus/rail, connects all ports together
class Bus final : public Component {
public:
    PORTS(Bus, v)

    Bus() = default;
    explicit Bus(uint32_t idx) : v_idx(idx) {}

    [[nodiscard]] std::string_view type_name() const override { return "Bus"; }
    void solve_electrical(SimulationState& state, float /*dt*/) override {
        // Bus is just a wire - no component behavior
    }
};

/// Generator - voltage source like battery
class Generator final : public Component {
public:
    PORTS(Generator, v_in, v_out)
    float internal_r = 0.005f;
    float v_nominal = 28.5f;

    Generator() = default;
    Generator(uint32_t v_in, uint32_t v_out, float v_nom, float int_r)
        : v_in_idx(v_in), v_out_idx(v_out), v_nominal(v_nom), internal_r(int_r) {}

    [[nodiscard]] std::string_view type_name() const override { return "Generator"; }
    void solve_electrical(SimulationState& state, float dt) override;
};

/// GS24 - Starter-Generator (ГС-24) with full state machine
class GS24 final : public Component {
public:
    PORTS(GS24, k_mod, v_in, v_out)
    // Mode state machine
    GS24Mode mode = GS24Mode::STARTER;
    float start_time = 0.0f;       // time since start
    float wait_time = 0.0f;       // time in wait state

    // Motor mode parameters
    float r_internal = 0.025f;     // internal resistance (~0.025 Ohm)
    float k_motor = 0.5f;          // back-EMF constant
    float i_max_starter = 800.0f;  // max starter current (800A)
    float rpm_cutoff = 0.45f;      // RPM cutoff to switch to generator (45%)

    // Generator mode parameters
    float v_nominal = 28.5f;       // nominal voltage (for reference, not used in calculations)
    float r_norton = 0.08f;        // Norton equivalent resistance (~0.08 Ohm)
    float target_rpm = 15000.0f;   // target RPM at 100%
    float current_rpm = 0.0f;      // current RPM
    float i_max = 400.0f;          // max generator current (400A)
    float rpm_threshold = 0.4f;    // 40% RPM threshold for self-excitation

    GS24() = default;
    GS24(uint32_t v_in, uint32_t v_out, float v_nom, float int_r)
        : v_in_idx(v_in), v_out_idx(v_out), v_nominal(v_nom), r_internal(int_r) {}

    [[nodiscard]] std::string_view type_name() const override { return "GS24"; }
    void solve_electrical(SimulationState& state, float dt) override;
    void post_step(SimulationState& state, float dt) override;
};

/// Transformer - AC transformer with voltage ratio
class Transformer final : public Component {
public:
    PORTS(Transformer, primary, secondary)
    float ratio = 1.0f;  // primary / secondary

    Transformer() = default;
    Transformer(uint32_t primary, uint32_t secondary, float r)
        : primary_idx(primary), secondary_idx(secondary), ratio(r) {}

    [[nodiscard]] std::string_view type_name() const override { return "Transformer"; }
    void solve_electrical(SimulationState& state, float dt) override;
};

/// Inverter - DC to AC inverter
class Inverter final : public Component {
public:
    PORTS(Inverter, ac_out, dc_in)
    float efficiency = 0.95f;
    float frequency = 400.0f;

    Inverter(uint32_t dc_in, uint32_t ac_out, float eff, float freq)
        : dc_in_idx(dc_in), ac_out_idx(ac_out), efficiency(eff), frequency(freq) {}

    [[nodiscard]] std::string_view type_name() const override { return "Inverter"; }
    void solve_electrical(SimulationState& state, float dt) override;
};

/// LerpNode - linear interpolation (voltage display filter)
class LerpNode final : public Component {
public:
    PORTS(LerpNode, input, output)
    float factor = 1.0f;  // filter coefficient (0.1 = slow, 1.0 = instant)

    LerpNode() = default;
    LerpNode(uint32_t input, uint32_t output, float f)
        : input_idx(input), output_idx(output), factor(f) {}

    [[nodiscard]] std::string_view type_name() const override { return "LerpNode"; }
    void solve_electrical(SimulationState& state, float dt) override;
    void post_step(SimulationState& state, float dt) override;
};

/// Splitter - 1-to-2 signal splitter (works with any type/domain)
class Splitter final : public Component {
public:
    PORTS(Splitter, i)

    Splitter() = default;
    explicit Splitter(uint32_t idx) : i_idx(idx) {}

    [[nodiscard]] std::string_view type_name() const override { return "Splitter"; }

    // Splitter is just a wire junction - union-find merges all alias ports to 'i'
    void solve_electrical(SimulationState& state, float /*dt*/) override {}
    void solve_mechanical(SimulationState& state, float /*dt*/) override {}
    void solve_hydraulic(SimulationState& state, float /*dt*/) override {}
    void solve_thermal(SimulationState& state, float /*dt*/) override {}
};

/// IndicatorLight - aircraft indicator light (two power terminals + brightness output)
class IndicatorLight final : public Component {
public:
    PORTS(IndicatorLight, brightness, v_in, v_out)
    float max_brightness = 100.0f;
    std::string color = "white";

    IndicatorLight() = default;
    IndicatorLight(uint32_t v_in, uint32_t v_out, uint32_t brightness, float max_bright, std::string c)
        : v_in_idx(v_in), v_out_idx(v_out), brightness_idx(brightness), max_brightness(max_bright), color(std::move(c)) {}

    [[nodiscard]] std::string_view type_name() const override { return "IndicatorLight"; }
    void solve_electrical(SimulationState& state, float dt) override;
};

/// HighPowerLoad - high power electrical load
class HighPowerLoad final : public Component {
public:
    PORTS(HighPowerLoad, v_in, v_out)
    float power_draw = 500.0f;

    HighPowerLoad(uint32_t v_in, uint32_t v_out, float power)
        : v_in_idx(v_in), v_out_idx(v_out), power_draw(power) {}

    [[nodiscard]] std::string_view type_name() const override { return "HighPowerLoad"; }
    void solve_electrical(SimulationState& state, float dt) override;
};

// =============================================================================
// Sensors (Electrical input, no output)
// =============================================================================

/// Voltmeter - analog voltage gauge (visual measurement only)
class Voltmeter final : public Component {
public:
    PORTS(Voltmeter, v_in)

    Voltmeter() = default;
    explicit Voltmeter(uint32_t v_in) : v_in_idx(v_in) {}

    [[nodiscard]] std::string_view type_name() const override { return "Voltmeter"; }
    void solve_electrical(SimulationState& state, float /*dt*/) override {
        // Voltmeter is purely visual - doesn't affect the circuit
        // It just reads the voltage for display purposes
        // No conductance, no current draw
    }
};

/// Gyroscope - power-only sensor
class Gyroscope final : public Component {
public:
    PORTS(Gyroscope, input)
    float conductance = 0.001f;

    Gyroscope() = default;
    explicit Gyroscope(uint32_t input) : input_idx(input) {}

    [[nodiscard]] std::string_view type_name() const override { return "Gyroscope"; }
    void solve_electrical(SimulationState& state, float dt) override;
};

/// AGK47 - attitude gyro
class AGK47 final : public Component {
public:
    PORTS(AGK47, input)
    float conductance = 0.001f;

    AGK47() = default;
    explicit AGK47(uint32_t input) : input_idx(input) {}

    [[nodiscard]] std::string_view type_name() const override { return "AGK47"; }
    void solve_electrical(SimulationState& state, float dt) override;
};

// =============================================================================
// Hydraulic Components (multi-domain)
// =============================================================================

/// ElectricPump - electric motor driving hydraulic pump
class ElectricPump final : public Component {
public:
    PORTS(ElectricPump, p_out, v_in)
    float max_pressure = 1000.0f;

    ElectricPump(uint32_t v_in, uint32_t p_out, float max_p)
        : v_in_idx(v_in), p_out_idx(p_out), max_pressure(max_p) {}

    [[nodiscard]] std::string_view type_name() const override { return "ElectricPump"; }
    void solve_electrical(SimulationState& state, float dt) override;
    void solve_hydraulic(SimulationState& state, float dt) override;
};

/// SolenoidValve - electrically controlled hydraulic valve
class SolenoidValve final : public Component {
public:
    PORTS(SolenoidValve, ctrl, flow_in, flow_out)
    bool normally_closed = true;

    SolenoidValve(uint32_t ctrl, uint32_t flow_in, uint32_t flow_out, bool nc)
        : ctrl_idx(ctrl), flow_in_idx(flow_in), flow_out_idx(flow_out), normally_closed(nc) {}

    [[nodiscard]] std::string_view type_name() const override { return "SolenoidValve"; }
    void solve_hydraulic(SimulationState& state, float dt) override;
};

// =============================================================================
// Mechanical Components
// =============================================================================

/// InertiaNode - mechanical inertia (first-order lag)
class InertiaNode final : public Component {
public:
    PORTS(InertiaNode, input, output)
    float mass = 1.0f;
    float inv_mass = 1.0f;
    float damping = 0.5f;

    InertiaNode(uint32_t input, uint32_t output, float m, float d)
        : input_idx(input), output_idx(output), mass(m), damping(d) {
        inv_mass = (m > 0.0f) ? 1.0f / m : 0.0f;
    }

    [[nodiscard]] std::string_view type_name() const override { return "InertiaNode"; }
    void solve_mechanical(SimulationState& state, float dt) override;
    void pre_load() override;
};

// =============================================================================
// Thermal Components
// =============================================================================

/// TempSensor - temperature sensor
class TempSensor final : public Component {
public:
    PORTS(TempSensor, temp_in, temp_out)
    float sensitivity = 1.0f;

    TempSensor(uint32_t temp_in, uint32_t temp_out, float sens)
        : temp_in_idx(temp_in), temp_out_idx(temp_out), sensitivity(sens) {}

    [[nodiscard]] std::string_view type_name() const override { return "TempSensor"; }
    void solve_thermal(SimulationState& state, float dt) override;
};

/// ElectricHeater - electrical heater
class ElectricHeater final : public Component {
public:
    PORTS(ElectricHeater, heat_out, power)
    float max_power = 1000.0f;
    float efficiency = 0.9f;

    ElectricHeater(uint32_t power, uint32_t heat_out, float max_p, float eff)
        : power_idx(power), heat_out_idx(heat_out), max_power(max_p), efficiency(eff) {}

    [[nodiscard]] std::string_view type_name() const override { return "ElectricHeater"; }
    void solve_electrical(SimulationState& state, float dt) override;
    void solve_thermal(SimulationState& state, float dt) override;
};

/// RUG-82 - Coal column voltage regulator (Угольный регулятор напряжения)
/// RUG-82 - Coal column voltage regulator (Norton model)
/// Input: v_gen (bus voltage), Output: k_mod (0...1 excitation modulation)
class RUG82 final : public Component {
public:
    PORTS(RUG82, k_mod, v_gen)
    float v_target = 28.5f;   // target voltage (28.5V)
    float k_mod = 0.5f;       // current modulation factor (0...1)
    float kp = 2.0f;          // proportional gain

    RUG82() = default;
    RUG82(uint32_t v_gen, uint32_t k_mod)
        : v_gen_idx(v_gen), k_mod_idx(k_mod) {}

    [[nodiscard]] std::string_view type_name() const override { return "RUG82"; }
    void solve_electrical(SimulationState& state, float dt) override;
};

/// DMR-400 - Differential Minimum Relay (Дифференциально-минимальное реле)
/// Connects generator to DC bus when ready, disconnects on reverse current
class DMR400 final : public Component {
public:
    PORTS(DMR400, lamp, v_gen_ref, v_in, v_out)
    bool is_closed = false;          // contactor state (default open)
    float connect_threshold = 2.0f;     // V_gen > V_bus + 2.0V to connect (hysteresis)
    float disconnect_threshold = 10.0f; // V_bus > V_gen + 10V to disconnect (reverse current)
    float min_voltage_to_close = 20.0f; // minimum generator voltage to close (starter must be done)
    float reconnect_delay = 0.0f;      // delay before reconnecting

    DMR400() = default;
    DMR400(uint32_t v_gen_ref, uint32_t v_in, uint32_t v_out, uint32_t lamp)
        : v_gen_ref_idx(v_gen_ref), v_in_idx(v_in), v_out_idx(v_out), lamp_idx(lamp) {}

    [[nodiscard]] std::string_view type_name() const override { return "DMR400"; }
    void solve_electrical(SimulationState& state, float dt) override;
    void post_step(SimulationState& state, float dt) override;
};

/// RU19A-300 - Auxiliary Power Unit (ВСУ)
/// Combines: GS24 starter-generator + start sequence automation
class RU19A final : public Component {
public:
    // Electrical ports (auto-generated from components/RU19A.json)
    // Port order must match registry: k_mod, rpm_out, t4_out, v_bus, v_start
    PORTS(RU19A, k_mod, rpm_out, t4_out, v_bus, v_start)

    // State machine
    APUState state = APUState::OFF;
    float timer = 0.0f;           // state timer

    // GS24 parameters (embedded)
    float target_rpm = 16000.0f; // max RPM
    float current_rpm = 0.0f;     // current RPM

    // Turbine inertia parameters
    float spinup_inertia = 1.0f;    // How fast RPM increases (spool up) - 100% per second at 20Hz
    float spindown_inertia = 0.02f;  // How fast RPM decreases (coast down)

    // Start sequence parameters
    float crank_time = 2.0f;     // time before ignition (sec)
    float ignition_time = 3.0f;  // ignition duration (sec)
    float runup_time = 8.0f;     // time to reach idle (sec)
    float start_timeout = 30.0f; // max start time (sec)

    // Thermal (T4 - gas temperature after turbine)
    float t4 = 0.0f;               // current T4 (Celsius)
    float t4_target = 400.0f;      // target T4 at idle
    float t4_max = 750.0f;         // max safe T4 (emergency cutoff)
    float ambient_temp = 20.0f;     // ambient temperature

    // Control
    bool auto_start = true;      // auto-start when battery connected

    void start() { if (state == APUState::OFF) state = APUState::CRANKING; }
    void stop() { state = APUState::STOPPING; }
    bool is_starter_active() const { return state == APUState::CRANKING || state == APUState::IGNITION; }

    RU19A() = default;
    RU19A(uint32_t v_start, uint32_t v_bus, uint32_t k_mod, uint32_t rpm_out, uint32_t t4_out)
        : v_start_idx(v_start), v_bus_idx(v_bus), k_mod_idx(k_mod), rpm_out_idx(rpm_out), t4_out_idx(t4_out) {}

    [[nodiscard]] std::string_view type_name() const override { return "RU19A"; }
    void solve_electrical(SimulationState& state, float dt) override;
    void solve_mechanical(SimulationState& state, float dt) override;
    void solve_thermal(SimulationState& state, float dt) override;
    void post_step(SimulationState& state, float dt) override;
};

/// Radiator - heat exchanger
class Radiator final : public Component {
public:
    PORTS(Radiator, heat_in, heat_out)
    float cooling_capacity = 1000.0f;

    Radiator(uint32_t heat_in, uint32_t heat_out, float capacity)
        : heat_in_idx(heat_in), heat_out_idx(heat_out), cooling_capacity(capacity) {}

    [[nodiscard]] std::string_view type_name() const override { return "Radiator"; }
    void solve_thermal(SimulationState& state, float dt) override;
};

// =============================================================================
// Logical Components
// =============================================================================

/// Comparator - voltage comparator with hysteresis
/// Inputs: Va (signal A), Vb (signal B)
/// Parameters: Von (turn-on threshold), Voff (turn-off threshold)
/// Output: o (boolean) - TRUE when (Va - Vb) > Von, FALSE when (Va - Vb) < Voff
/// Maintains state when in hysteresis band (between Voff and Von)
class Comparator final : public Component {
public:
    PORTS(Comparator, Va, Vb, o)
    bool output_state = false;  // Current boolean output state
    float Von = 5.0f;          // Turn-on threshold (parameter)
    float Voff = 2.0f;         // Turn-off threshold (parameter)

    Comparator() = default;
    Comparator(uint32_t Va, uint32_t Vb, uint32_t o)
        : Va_idx(Va), Vb_idx(Vb), o_idx(o) {}

    [[nodiscard]] std::string_view type_name() const override { return "Comparator"; }
    void solve_logical(SimulationState& state, float dt) override;
    void pre_load() override;
};

} // namespace an24
