#pragma once

#include "component.h"
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

/// Switch - manual toggle switch (triggered by control signal)
/// Switch - manual toggle switch (triggered by control signal)
/// Mirrors downstream conductance onto v_in so battery sees the load.
/// post_step forces v_out=v_in when closed, v_out=0 when open.
class Switch : public Component {
public:
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    uint32_t control_idx = 0;  // Control trigger (any change toggles state)
    uint32_t state_idx = 0;    // State output (1.0V = closed, 0.0V = open)
    bool closed = false;        // Initial state (default: open)
    float last_control = 0.0f;  // Previous control voltage (edge detection)
    float downstream_g = 0.0f;  // Cached downstream conductance (from prev step)
    float downstream_I = 0.0f;  // Cached downstream Norton current (from prev step)
    float v_out_old = 0.0f;     // V_out at start of step (before SOR)

    Switch() = default;
    Switch(uint32_t v_in, uint32_t v_out, uint32_t control, uint32_t state, bool is_closed = false)
        : v_in_idx(v_in), v_out_idx(v_out), control_idx(control), state_idx(state), closed(is_closed), last_control(0.0f) {}

    [[nodiscard]] std::string_view type_name() const override { return "Switch"; }
    void solve_electrical(SimulationState& state) override;
    void post_step(SimulationState& state, float dt) override;
};

/// Relay - on/off switch controlled by voltage threshold.
/// Mirrors downstream conductance onto v_in so battery sees the load.
class Relay : public Component {
public:
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    uint32_t control_idx = 0;  // Control signal (closed while control > threshold)
    bool closed = false;        // Current state
    float hold_threshold = 0.5f;  // Voltage threshold to hold closed
    float downstream_g = 0.0f;  // Cached downstream conductance (from prev step)
    float downstream_I = 0.0f;  // Cached downstream Norton current (from prev step)
    float v_out_old = 0.0f;     // V_out at start of step (before SOR)

    Relay() = default;
    Relay(uint32_t v_in, uint32_t v_out, uint32_t control, bool is_closed = false, float threshold = 0.5f)
        : v_in_idx(v_in), v_out_idx(v_out), control_idx(control), closed(is_closed), hold_threshold(threshold) {}

    [[nodiscard]] std::string_view type_name() const override { return "Relay"; }
    void solve_electrical(SimulationState& state) override;
    void post_step(SimulationState& state, float dt) override;
};

/// HoldButton - hold-to-operate button with press/release detection.
/// Mirrors downstream conductance onto v_in so battery sees the load.
/// Control Protocol: 0.0V=Idle, 1.0V=Pressed, 2.0V=Released
/// State output: 1.0V = pressed, 0.0V = released/idle
class HoldButton : public Component {
public:
    uint32_t v_in_idx = 0;      // Voltage input (passed through when pressed)
    uint32_t v_out_idx = 0;     // Voltage output
    uint32_t control_idx = 0;   // Control input (commands from UI)
    uint32_t state_idx = 0;      // State output: 1.0V=pressed, 0.0V=released
    float last_control = 0.0f;   // Previous control value (edge detection)
    bool is_pressed = false;     // Current button state (latched)
    float downstream_g = 0.0f;  // Cached downstream conductance (from prev step)
    float downstream_I = 0.0f;  // Cached downstream Norton current (from prev step)
    float v_out_old = 0.0f;     // V_out at start of step (before SOR)

    HoldButton() = default;
    HoldButton(uint32_t v_in, uint32_t v_out, uint32_t control, uint32_t state)
        : v_in_idx(v_in), v_out_idx(v_out), control_idx(control), state_idx(state), last_control(0.0f), is_pressed(false) {}

    [[nodiscard]] std::string_view type_name() const override { return "HoldButton"; }
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

/// GS24 - Starter-Generator (ГС-24) with full state machine
class GS24 : public Component {
public:
    uint32_t v_in_idx = 0;        // ground input
    uint32_t v_out_idx = 0;        // bus output (generator mode)
    uint32_t k_mod_idx = 0;        // excitation modulation from RUG82 (0...1)

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
    void solve_electrical(SimulationState& state) override;
    void post_step(SimulationState& state, float dt) override;
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
    float factor = 1.0f;  // filter coefficient (0.1 = slow, 1.0 = instant)

    LerpNode() = default;
    LerpNode(uint32_t input, uint32_t output, float f)
        : input_idx(input), output_idx(output), factor(f) {}

    [[nodiscard]] std::string_view type_name() const override { return "LerpNode"; }
    void solve_electrical(SimulationState& state) override;
    void post_step(SimulationState& state, float dt) override;
};

/// IndicatorLight - aircraft indicator light (two power terminals + brightness output)
class IndicatorLight : public Component {
public:
    uint32_t v_in_idx = 0;        // power input (bus side)
    uint32_t v_out_idx = 0;       // power return (ground side)
    uint32_t brightness_idx = 0;  // brightness output signal
    float max_brightness = 100.0f;
    std::string color = "white";

    IndicatorLight() = default;
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

/// RUG-82 - Coal column voltage regulator (Угольный регулятор напряжения)
/// RUG-82 - Coal column voltage regulator (Norton model)
/// Input: v_gen (bus voltage), Output: k_mod (0...1 excitation modulation)
class RUG82 : public Component {
public:
    uint32_t v_gen_idx = 0;   // input: generator voltage (U_bus)
    uint32_t k_mod_idx = 0;   // output: excitation modulation (0...1)
    float v_target = 28.5f;   // target voltage (28.5V)
    float k_mod = 0.5f;       // current modulation factor (0...1)
    float kp = 2.0f;          // proportional gain

    RUG82() = default;
    RUG82(uint32_t v_gen, uint32_t k_mod)
        : v_gen_idx(v_gen), k_mod_idx(k_mod) {}

    [[nodiscard]] std::string_view type_name() const override { return "RUG82"; }
    void solve_electrical(SimulationState& state) override;
};

/// DMR-400 - Differential Minimum Relay (Дифференциально-минимальное реле)
/// Connects generator to DC bus when ready, disconnects on reverse current
class DMR400 : public Component {
public:
    uint32_t v_gen_in_idx = 0;     // input: generator voltage
    uint32_t v_bus_mon_idx = 0;    // input: bus voltage monitoring (battery side)
    uint32_t v_out_idx = 0;        // output: connected to bus
    uint32_t lamp_idx = 0;         // output: warning lamp (1 = ON when disconnected)

    bool is_closed = false;          // contactor state (default open)
    float connect_threshold = 2.0f;     // V_gen > V_bus + 2.0V to connect (hysteresis)
    float disconnect_threshold = 10.0f; // V_bus > V_gen + 10V to disconnect (reverse current)
    float min_voltage_to_close = 20.0f; // minimum generator voltage to close (starter must be done)
    float reconnect_delay = 0.0f;      // delay before reconnecting

    DMR400() = default;
    DMR400(uint32_t v_gen_in, uint32_t v_bus_mon, uint32_t v_out, uint32_t lamp)
        : v_gen_in_idx(v_gen_in), v_bus_mon_idx(v_bus_mon), v_out_idx(v_out), lamp_idx(lamp) {}

    [[nodiscard]] std::string_view type_name() const override { return "DMR400"; }
    void solve_electrical(SimulationState& state) override;
    void post_step(SimulationState& state, float dt) override;
};

/// RU19A-300 - Auxiliary Power Unit (ВСУ)
/// Combines: GS24 starter-generator + start sequence automation
class RU19A : public Component {
public:
    // Electrical ports
    uint32_t v_start_idx = 0;    // starter power input (direct from battery, bypasses DMR)
    uint32_t v_out_idx = 0;       // bus voltage output (goes through DMR)
    uint32_t k_mod_idx = 0;       // excitation modulation to GS24

    // Status output ports
    uint32_t rpm_out_idx = 0;      // RPM output signal
    uint32_t t4_out_idx = 0;       // T4 temperature output signal

    // State machine
    APUState state = APUState::OFF;
    float timer = 0.0f;           // state timer

    // GS24 parameters (embedded)
    float target_rpm = 16000.0f; // max RPM
    float current_rpm = 0.0f;     // current RPM

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

    // Thermal model parameters
    float heating_rate = 120.0f;    // heating at ignition (deg/sec)
    float base_cooling = 110.0f;    // cooling at 60% RPM
    float thermal_stress_factor = 0.5f;  // stress when RPM < 40%

    // Thermal solver call counter
    int thermal_counter = 0;

    // Control
    bool auto_start = true;      // auto-start when battery connected

    void start() { if (state == APUState::OFF) state = APUState::CRANKING; }
    void stop() { state = APUState::STOPPING; }
    bool is_starter_active() const { return state == APUState::CRANKING || state == APUState::IGNITION; }

    RU19A() = default;
    RU19A(uint32_t v_start, uint32_t v_out, uint32_t k_mod, uint32_t rpm_out, uint32_t t4_out)
        : v_start_idx(v_start), v_out_idx(v_out), k_mod_idx(k_mod), rpm_out_idx(rpm_out), t4_out_idx(t4_out) {}

    [[nodiscard]] std::string_view type_name() const override { return "RU19A"; }
    void solve_electrical(SimulationState& state) override;
    void solve_thermal(SimulationState& state) override;
    void post_step(SimulationState& state, float dt) override;
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
