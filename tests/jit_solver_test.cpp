#include <gtest/gtest.h>
#include "jit_solver/jit_solver.h"
#include "jit_solver/state.h"
#include "jit_solver/components/all.h"
#include "json_parser/json_parser.h"

using namespace an24;

// =============================================================================
// Helper: create DeviceInstance with common fields
// =============================================================================
static DeviceInstance make_device(
    const std::string& name,
    const std::string& classname,
    std::unordered_map<std::string, std::string> params = {},
    std::unordered_map<std::string, PortDirection> ports = {}
) {
    DeviceInstance dev;
    dev.name = name;
    dev.classname = classname;
    dev.params = std::move(params);
    for (const auto& [port_name, dir] : ports) {
        dev.ports[port_name] = Port{dir};
    }
    return dev;
}

// =============================================================================
// Helper: run SOR simulation to steady state
// =============================================================================
static SimulationState run_sor(
    BuildResult& result,
    const std::vector<DeviceInstance>& devices,
    int steps = 100,
    float omega = 1.5f
) {
    SimulationState state;

    // Allocate signals
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(), result.fixed_signals.end(), i);
        state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Set fixed signal values from RefNode devices
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) value = std::stof(it_val->second);

            std::string port = dev.name + ".v";
            auto it_sig = result.port_to_signal.find(port);
            if (it_sig != result.port_to_signal.end()) {
                state.across[it_sig->second] = value;
            }
        }
    }

    // SOR iteration
    for (int step = 0; step < steps; ++step) {
        state.clear_through();
        result.systems.solve_step(state, step);
        state.precompute_inv_conductance();

        for (size_t i = 0; i < state.across.size(); ++i) {
            if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
                state.across[i] += state.through[i] * state.inv_conductance[i] * omega;
            }
        }

        // Post-step: relay switches copy voltage after SOR update
        result.systems.post_step(state, 1.0f / 60.0f);
    }

    return state;
}

// Helper to get signal voltage by port name
static float get_voltage(const SimulationState& state, const BuildResult& result,
                          const std::string& port_name) {
    auto it = result.port_to_signal.find(port_name);
    EXPECT_NE(it, result.port_to_signal.end()) << "Port not found: " << port_name;
    return state.across[it->second];
}

// =============================================================================
// Unit Tests - SimulationState
// =============================================================================

TEST(SimulationStateTest, AllocateSignal) {
    SimulationState state;

    auto idx = state.allocate_signal(12.0f, {Domain::Electrical, false});

    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(state.across.size(), 1u);
    EXPECT_FLOAT_EQ(state.across[idx], 12.0f);
}

TEST(SimulationStateTest, AllocateMultipleSignals) {
    SimulationState state;

    auto idx0 = state.allocate_signal(0.0f, {Domain::Electrical, true});
    auto idx1 = state.allocate_signal(28.0f, {Domain::Electrical, false});
    auto idx2 = state.allocate_signal(15.0f, {Domain::Hydraulic, false});

    EXPECT_EQ(idx0, 0u);
    EXPECT_EQ(idx1, 1u);
    EXPECT_EQ(idx2, 2u);
    EXPECT_EQ(state.across.size(), 3u);
    EXPECT_TRUE(state.signal_types[0].is_fixed);
    EXPECT_FALSE(state.signal_types[1].is_fixed);
    EXPECT_EQ(state.signal_types[2].domain, Domain::Hydraulic);
}

TEST(SimulationStateTest, ClearThrough) {
    SimulationState state;

    state.allocate_signal(0.0f, {Domain::Electrical, false});
    state.through[0] = 5.0f;
    state.conductance[0] = 2.0f;

    state.clear_through();

    EXPECT_FLOAT_EQ(state.through[0], 0.0f);
    EXPECT_FLOAT_EQ(state.conductance[0], 0.0f);
}

TEST(SimulationStateTest, PrecomputeInvConductance) {
    SimulationState state;

    state.allocate_signal(0.0f, {Domain::Electrical, true});   // fixed
    state.allocate_signal(0.0f, {Domain::Electrical, false});  // normal
    state.allocate_signal(0.0f, {Domain::Electrical, false});  // open circuit

    state.conductance[0] = 100.0f;
    state.conductance[1] = 4.0f;
    state.conductance[2] = 0.0f;

    state.precompute_inv_conductance();

    EXPECT_FLOAT_EQ(state.inv_conductance[0], 0.0f);    // fixed: inv_g = 0 (SOR skips)
    EXPECT_FLOAT_EQ(state.inv_conductance[1], 0.25f);   // 1/4
    EXPECT_FLOAT_EQ(state.inv_conductance[2], 0.0f);    // open circuit
}

// =============================================================================
// Unit Tests - build_systems_dev
// =============================================================================

TEST(BuildSystemsTest, BasicBuildSingleDevice) {
    DeviceInstance battery;
    battery.name = "bat_main_1";
    battery.classname = "Battery";
    battery.params["v_nominal"] = "28.0";
    battery.ports["v_in"] = Port{PortDirection::In};
    battery.ports["v_out"] = Port{PortDirection::Out};

    std::vector<DeviceInstance> devices = {battery};
    std::vector<std::pair<std::string, std::string>> connections;

    auto result = build_systems_dev(devices, connections);

    EXPECT_GT(result.signal_count, 0u);
    EXPECT_EQ(result.systems.component_count(), 1u);
}

TEST(BuildSystemsTest, ConnectionsMergeSignals) {
    // Two devices connected: their shared ports should map to the same signal
    auto battery = make_device("bat", "Battery",
        {{"v_nominal", "28.0"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto relay = make_device("relay", "Relay", {},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"control", PortDirection::In}});
    auto control = make_device("ctrl", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {battery, relay, control};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat.v_out", "relay.v_in"},
        {"ctrl.v", "relay.control"},  // Control = 0V (relay open)
    };

    auto result = build_systems_dev(devices, connections);

    // bat.v_out and relay.v_in should be the same signal
    EXPECT_EQ(result.port_to_signal["bat.v_out"],
              result.port_to_signal["relay.v_in"]);

    // bat.v_in and relay.v_out should be different signals
    EXPECT_NE(result.port_to_signal["bat.v_in"],
              result.port_to_signal["relay.v_out"]);
}

TEST(BuildSystemsTest, RefNodeMarkedFixed) {
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd};
    std::vector<std::pair<std::string, std::string>> connections;

    auto result = build_systems_dev(devices, connections);

    EXPECT_EQ(result.fixed_signals.size(), 1u);
    EXPECT_EQ(result.fixed_signals[0], result.port_to_signal["gnd.v"]);
}

// =============================================================================
// REGRESSION: Signal separation - bus vs ground must not merge
// =============================================================================
// This was the core bug: connecting IndicatorLight.power to both bus AND ground
// caused Union-Find to merge all signals. With two-terminal lights (v_in, v_out),
// bus and ground remain separate.

TEST(RegressionTest, BusAndGroundSignalsSeparate) {
    // Reproduce the exact failing topology from test_full_dc_bus
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto dc_bus = make_device("dc_bus", "Bus", {{}}, {{"v", PortDirection::Out}});
    auto battery = make_device("battery", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto light1 = make_device("light1", "IndicatorLight", {{"max_brightness", "100.0"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"brightness", PortDirection::Out}});
    auto light2 = make_device("light2", "IndicatorLight", {{"max_brightness", "100.0"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"brightness", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, dc_bus, battery, light1, light2};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "dc_bus.v"},
        {"dc_bus.v", "light1.v_in"},
        {"dc_bus.v", "light2.v_in"},
        {"light1.v_out", "gnd.v"},
        {"light2.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
    };

    auto result = build_systems_dev(devices, connections);

    // CRITICAL: bus and ground must be DIFFERENT signals
    uint32_t bus_signal = result.port_to_signal["dc_bus.v"];
    uint32_t gnd_signal = result.port_to_signal["gnd.v"];
    EXPECT_NE(bus_signal, gnd_signal)
        << "BUG: Bus and ground collapsed into same signal via Union-Find";

    // Bus-side ports should all share the same signal
    EXPECT_EQ(result.port_to_signal["battery.v_out"], bus_signal);
    EXPECT_EQ(result.port_to_signal["light1.v_in"], bus_signal);
    EXPECT_EQ(result.port_to_signal["light2.v_in"], bus_signal);

    // Ground-side ports should all share the same signal
    EXPECT_EQ(result.port_to_signal["battery.v_in"], gnd_signal);
    EXPECT_EQ(result.port_to_signal["light1.v_out"], gnd_signal);
    EXPECT_EQ(result.port_to_signal["light2.v_out"], gnd_signal);
}

TEST(RegressionTest, DCBusVoltageNonZero) {
    // The original bug produced 0V at all nodes because everything was one signal.
    // After fix, bus voltage should be ~26.2V (28V battery, 0.1Ω int, 2x 0.35S loads).
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto dc_bus = make_device("dc_bus", "Bus", {{}}, {{"v", PortDirection::Out}});
    auto battery = make_device("battery", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto light1 = make_device("light1", "IndicatorLight", {{"max_brightness", "100.0"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"brightness", PortDirection::Out}});
    auto light2 = make_device("light2", "IndicatorLight", {{"max_brightness", "100.0"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"brightness", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, dc_bus, battery, light1, light2};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "dc_bus.v"},
        {"dc_bus.v", "light1.v_in"},
        {"dc_bus.v", "light2.v_in"},
        {"light1.v_out", "gnd.v"},
        {"light2.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);

    float v_bus = get_voltage(state, result, "dc_bus.v");
    float v_gnd = get_voltage(state, result, "gnd.v");

    // Ground must stay at 0V
    EXPECT_FLOAT_EQ(v_gnd, 0.0f);

    // Bus voltage must be non-zero (was 0V before fix)
    EXPECT_GT(v_bus, 20.0f) << "Bus voltage too low - signal merging bug may have returned";

    // Expected: V = 28 * (1/0.7) / (0.1 + 1/0.7) ≈ 26.17V
    EXPECT_NEAR(v_bus, 26.17f, 1.0f);
}

TEST(RegressionTest, ResistorVoltageDifference) {
    // Verify Resistor correctly uses voltage difference (v_in - v_out),
    // not just v_in. This matters when neither terminal is ground.
    //
    // Circuit: Battery(28V) -> R1(1Ω) -> R2(1Ω) -> Ground
    // Two equal resistors in series: each drops half the voltage.
    auto gnd = make_device("gnd", "RefNode",
        {{"value", "0.0"}},
        {{"v", PortDirection::Out}});
    auto battery = make_device("battery", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.001"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto r1 = make_device("r1", "Resistor",
        {{"conductance", "1.0"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto r2 = make_device("r2", "Resistor",
        {{"conductance", "1.0"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, battery, r1, r2};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "r1.v_in"},
        {"r1.v_out", "r2.v_in"},
        {"r2.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);

    float v_bus = get_voltage(state, result, "battery.v_out");
    float v_mid = get_voltage(state, result, "r1.v_out");  // junction between resistors
    float v_gnd = get_voltage(state, result, "gnd.v");

    EXPECT_FLOAT_EQ(v_gnd, 0.0f);

    // With two equal resistors in series and near-ideal source:
    // V_mid ≈ V_bus / 2, V_bus ≈ 28V
    EXPECT_NEAR(v_bus, 28.0f, 0.5f);
    EXPECT_NEAR(v_mid, 14.0f, 1.0f)
        << "Mid-point voltage wrong - resistor may not use (v_in - v_out)";
}

TEST(RegressionTest, IndicatorLightTwoTerminals) {
    // Verify IndicatorLight works as a two-terminal device:
    // voltage difference drives brightness, not absolute voltage.
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto bus = make_device("bus", "RefNode", {{"value", "28.0"}}, {{"v", PortDirection::Out}});
    auto light = make_device("light", "IndicatorLight", {{"max_brightness", "100.0"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"brightness", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, bus, light};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"bus.v", "light.v_in"},
        {"light.v_out", "gnd.v"},
    };

    auto result = build_systems_dev(devices, connections);

    // Bus and ground must be separate signals
    EXPECT_NE(result.port_to_signal["bus.v"], result.port_to_signal["gnd.v"]);

    auto state = run_sor(result, devices, 100);

    float brightness = get_voltage(state, result, "light.brightness");

    // At 28V, brightness should be max (100)
    EXPECT_NEAR(brightness, 100.0f, 1.0f);
}

// =============================================================================
// Integration Tests - Complete Circuit Scenarios
// =============================================================================

TEST(IntegrationTest, BatteryWithResistiveLoad) {
    // Simple: Battery (28V, R_int=0.1Ω) → Load (R=10Ω) → Ground
    // V_load = 28 * 10 / (0.1 + 10) = 27.72V
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto battery = make_device("battery", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto load = make_device("load", "Resistor", {{"conductance", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, battery, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices);

    float v_load = get_voltage(state, result, "battery.v_out");
    EXPECT_NEAR(v_load, 27.72f, 0.1f);
}

TEST(IntegrationTest, GeneratorWithResistiveLoad) {
    // Generator (28.5V, R_int=0.1Ω) → Load (R=10Ω) → Ground
    // V_load = 28.5 * 10 / (0.1 + 10) ≈ 28.22V
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto gen = make_device("gen", "Generator", {{"v_nominal", "28.5"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto load = make_device("load", "Resistor", {{"conductance", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, gen, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"gen.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"},
        {"gen.v_in", "gnd.v"},
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices);

    float v_load = get_voltage(state, result, "gen.v_out");
    EXPECT_NEAR(v_load, 28.22f, 0.1f);
}

TEST(IntegrationTest, FullDCBusWithTwoLights) {
    // Battery (28V, 0.1Ω) → Bus → 2x IndicatorLight (0.35S each) → Ground
    // Total load: 0.7S → R_load = 1/0.7 ≈ 1.43Ω
    // V_bus = 28 × 1.43 / (0.1 + 1.43) ≈ 26.17V
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto dc_bus = make_device("dc_bus", "Bus", {{}}, {{"v", PortDirection::Out}});
    auto battery = make_device("battery", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto light1 = make_device("light1", "IndicatorLight", {{"max_brightness", "100.0"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"brightness", PortDirection::Out}});
    auto light2 = make_device("light2", "IndicatorLight", {{"max_brightness", "100.0"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"brightness", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, dc_bus, battery, light1, light2};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "dc_bus.v"},
        {"dc_bus.v", "light1.v_in"},
        {"dc_bus.v", "light2.v_in"},
        {"light1.v_out", "gnd.v"},
        {"light2.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);

    float v_bus = get_voltage(state, result, "dc_bus.v");
    float v_gnd = get_voltage(state, result, "gnd.v");
    float b1 = get_voltage(state, result, "light1.brightness");
    float b2 = get_voltage(state, result, "light2.brightness");

    EXPECT_FLOAT_EQ(v_gnd, 0.0f);
    EXPECT_NEAR(v_bus, 26.17f, 1.0f);

    // Both lights should have equal brightness
    EXPECT_NEAR(b1, b2, 0.01f);

    // Brightness: normalized voltage × 100
    float expected_brightness = (v_bus / 28.0f) * 100.0f;
    EXPECT_NEAR(b1, expected_brightness, 1.0f);
}

TEST(IntegrationTest, BatteryRelayChain) {
    // Battery → Relay (held closed by control) → Load (10Ω) → Ground
    // Closed relay is a zero-resistance switch: V_bus = V_after_relay.
    // V = 28 * 10 / (0.1 + 10) = 27.72V
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto battery = make_device("battery", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto control = make_device("ctrl", "RefNode", {{"value", "28.0"}}, {{"v", PortDirection::Out}});
    auto relay = make_device("relay", "Relay", {{}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"control", PortDirection::In}});
    auto load = make_device("load", "Resistor", {{"conductance", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, battery, control, relay, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "relay.v_in"},
        {"relay.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
        {"ctrl.v", "relay.control"},  // Control = 28V (relay held closed)
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);

    float v_bus = get_voltage(state, result, "battery.v_out");
    float v_after_relay = get_voltage(state, result, "relay.v_out");
    float v_gnd = get_voltage(state, result, "gnd.v");

    EXPECT_FLOAT_EQ(v_gnd, 0.0f);
    // Perfect switch: both sides at same voltage
    // V = 28 * 10 / (0.1 + 10) ≈ 27.72V
    EXPECT_NEAR(v_bus, 27.72f, 0.5f);
    EXPECT_NEAR(v_after_relay, v_bus, 0.1f)
        << "Closed relay should have equal voltage on both sides";
}

TEST(IntegrationTest, OpenRelayBlocksCurrent) {
    // Battery → Relay (open) → Load → Ground
    // Open relay = no current, so load voltage should be ~0V
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto battery = make_device("battery", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto relay = make_device("relay", "Relay", {{"closed", "false"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"control", PortDirection::In}});
    auto control_src = make_device("ctrl_src", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto load = make_device("load", "Resistor", {{"conductance", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, battery, relay, control_src, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "relay.v_in"},
        {"relay.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
        {"ctrl_src.v", "relay.control"},  // Control = 0V (relay stays open)
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);

    float v_bus = get_voltage(state, result, "battery.v_out");
    float v_after_relay = get_voltage(state, result, "relay.v_out");

    // Battery side should be near nominal (open circuit)
    EXPECT_NEAR(v_bus, 28.0f, 0.5f);
    // After open relay: 0V (no connection)
    EXPECT_NEAR(v_after_relay, 0.0f, 0.01f);
}

TEST(IntegrationTest, ParallelBatteriesShareLoad) {
    // Two batteries in parallel feeding one load.
    // Each battery: 28V, R_int=0.1Ω. Load: R=10Ω (G=0.1S).
    // Parallel R_int = 0.05Ω. V_load = 28 * 10 / (0.05 + 10) ≈ 27.86V.
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto bat1 = make_device("bat1", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::Out}, {"v_out", PortDirection::Out}});
    auto bat2 = make_device("bat2", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::Out}, {"v_out", PortDirection::Out}});
    auto load = make_device("load", "Resistor", {{"conductance", "0.1"}}, {{"v_in", PortDirection::Out}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, bat1, bat2, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat1.v_out", "load.v_in"},
        {"bat2.v_out", "load.v_in"},   // both batteries on same bus
        {"load.v_out", "gnd.v"},
        {"bat1.v_in", "gnd.v"},
        {"bat2.v_in", "gnd.v"},
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);

    float v_bus = get_voltage(state, result, "bat1.v_out");
    float v_gnd = get_voltage(state, result, "gnd.v");

    EXPECT_FLOAT_EQ(v_gnd, 0.0f);
    // V ≈ 28 * 10 / (0.05 + 10) = 27.86V
    EXPECT_NEAR(v_bus, 27.86f, 0.2f);
}

TEST(IntegrationTest, MultipleLightsWithBrightness) {
    // 3 lights on a bus, verify brightness proportional to voltage
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto battery = make_device("battery", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto l1 = make_device("l1", "IndicatorLight", {{"max_brightness", "100.0"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"brightness", PortDirection::Out}});
    auto l2 = make_device("l2", "IndicatorLight", {{"max_brightness", "50.0"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"brightness", PortDirection::Out}});
    auto l3 = make_device("l3", "IndicatorLight", {{"max_brightness", "200.0"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"brightness", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, battery, l1, l2, l3};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "l1.v_in"},
        {"battery.v_out", "l2.v_in"},
        {"battery.v_out", "l3.v_in"},
        {"l1.v_out", "gnd.v"},
        {"l2.v_out", "gnd.v"},
        {"l3.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);

    float v_bus = get_voltage(state, result, "battery.v_out");
    float b1 = get_voltage(state, result, "l1.brightness");
    float b2 = get_voltage(state, result, "l2.brightness");
    float b3 = get_voltage(state, result, "l3.brightness");

    // Bus should have reasonable voltage (3 loads)
    EXPECT_GT(v_bus, 15.0f);
    EXPECT_LT(v_bus, 28.0f);

    // Brightness should scale proportionally with max_brightness
    // All lights see same voltage, so normalized fraction is the same
    float normalized = v_bus / 28.0f;
    EXPECT_NEAR(b1, normalized * 100.0f, 1.0f);
    EXPECT_NEAR(b2, normalized * 50.0f, 0.5f);
    EXPECT_NEAR(b3, normalized * 200.0f, 2.0f);

    // Brightness ratios
    EXPECT_NEAR(b2 / b1, 0.5f, 0.01f);
    EXPECT_NEAR(b3 / b1, 2.0f, 0.01f);
}

TEST(IntegrationTest, SORConvergence) {
    // Verify that more SOR iterations don't significantly change the result
    // (solution has converged).
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto battery = make_device("battery", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto load = make_device("load", "Resistor", {{"conductance", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, battery, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
    };

    auto result_50 = build_systems_dev(devices, connections);
    auto state_50 = run_sor(result_50, devices, 50);
    float v_50 = get_voltage(state_50, result_50, "battery.v_out");

    auto result_200 = build_systems_dev(devices, connections);
    auto state_200 = run_sor(result_200, devices, 200);
    float v_200 = get_voltage(state_200, result_200, "battery.v_out");

    // After 50 and 200 steps, voltage should be nearly identical (converged)
    EXPECT_NEAR(v_50, v_200, 0.01f);
}

// =============================================================================
// REGRESSION: March 2026 - Electrical circuit topology fixes
// =============================================================================
// These tests verify the fixes for the unstable SOR solver issue where:
// 1. DC buses were incorrectly modeled as RefNode with fixed voltages
// 2. Relay had conflicting behavior (conductance + post_step voltage copy)
// 3. Multiple voltage sources with different values caused NaN explosion

TEST(RegressionTest, BusTypeNotRefNode) {
    // BUG: dc_bus_1 and dc_bus_2 were RefNode with value=28.0 and value=24.0
    // This caused them to be marked as fixed signals, creating conflicts when
    // connected through a closed relay.
    //
    // FIX: Buses should use classname="Bus" type - simple connectors without
    // fixed voltage. Only ground (gnd) should be RefNode with value=0.0.

    auto gnd = make_device("gnd", "RefNode",
        {{"value", "0.0"}},
        {{"v", PortDirection::Out}});
    auto bus1 = make_device("bus1", "Bus", {{}},
        {{"v", PortDirection::Out}});
    auto bus2 = make_device("bus2", "Bus", {{}},
        {{"v", PortDirection::Out}});
    auto bat1 = make_device("bat1", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.01"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto bat2 = make_device("bat2", "Battery",
        {{"v_nominal", "24.0"}, {"internal_r", "0.01"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto control = make_device("ctrl", "RefNode", {{"value", "28.0"}}, {{"v", PortDirection::Out}});
    auto relay = make_device("relay", "Relay", {{}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"control", PortDirection::In}});

    std::vector<DeviceInstance> devices = {gnd, bus1, bus2, bat1, bat2, control, relay};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat1.v_out", "bus1.v"},
        {"bat2.v_out", "bus2.v"},
        {"bus1.v", "relay.v_in"},
        {"relay.v_out", "bus2.v"},
        {"bat1.v_in", "gnd.v"},
        {"bat2.v_in", "gnd.v"},
        {"ctrl.v", "relay.control"},  // Control = 28V (relay held closed)
    };

    auto result = build_systems_dev(devices, connections);

    // CRITICAL: Only ground should be fixed, NOT the buses
    EXPECT_EQ(result.fixed_signals.size(), 1u)
        << "Only ground should be fixed; buses must float";
    EXPECT_EQ(result.fixed_signals[0], result.port_to_signal["gnd.v"]);

    // Buses and ground must be different signals
    uint32_t bus1_sig = result.port_to_signal["bus1.v"];
    uint32_t bus2_sig = result.port_to_signal["bus2.v"];
    uint32_t gnd_sig = result.port_to_signal["gnd.v"];

    EXPECT_NE(bus1_sig, gnd_sig);
    EXPECT_NE(bus2_sig, gnd_sig);

    // Note: bus1 and bus2 are connected through relay component, not directly.
    // The relay will equalize their voltages during simulation via post_step(),
    // but they remain separate signals in the build phase.
    // This is correct behavior - relay is a switch, not a wire.
}

TEST(RegressionTest, ClosedRelayNoConductance) {
    // BUG: Relay::solve_electrical() added conductance=1e6 S while post_step()
    // also copied voltage directly. This dual behavior caused SOR instability.
    //
    // FIX: Closed relay is handled ONLY in post_step() by direct voltage copy.
    // solve_electrical() should NOT add conductance for closed relay.

    auto gnd = make_device("gnd", "RefNode",
        {{"value", "0.0"}},
        {{"v", PortDirection::Out}});
    auto bus = make_device("bus", "Bus", {{}},
        {{"v", PortDirection::Out}});
    auto bat = make_device("bat", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.01"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto control_src = make_device("ctrl", "RefNode", {{"value", "28.0"}},
        {{"v", PortDirection::Out}});
    auto relay = make_device("relay", "Relay", {},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"control", PortDirection::In}});
    auto load = make_device("load", "Resistor",
        {{"conductance", "0.1"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, bus, bat, control_src, relay, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat.v_out", "bus.v"},
        {"bus.v", "relay.v_in"},
        {"relay.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"},
        {"bat.v_in", "gnd.v"},
        {"ctrl.v", "relay.control"},  // Control = 28V (relay closed)
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);

    float v_bus = get_voltage(state, result, "bus.v");
    float v_after_relay = get_voltage(state, result, "relay.v_out");
    float v_gnd = get_voltage(state, result, "gnd.v");

    EXPECT_FLOAT_EQ(v_gnd, 0.0f);

    // Closed relay should have equal voltage on both sides
    EXPECT_NEAR(v_after_relay, v_bus, 0.01f)
        << "Closed relay should have equal voltage on both sides";

    // Voltage should be stable (not NaN or exploding)
    EXPECT_FALSE(std::isnan(v_bus));
    EXPECT_FALSE(std::isinf(v_bus));
    EXPECT_GT(v_bus, 0.0f);
    EXPECT_LT(v_bus, 30.0f);
}

TEST(RegressionTest, DualBatteryWithRelayStable) {
    // BUG: Two batteries (28V and 24V) connected through relay caused SOR
    // to oscillate and produce NaN after ~5 iterations.
    // Root cause: both buses were RefNode with different fixed voltages.
    //
    // FIX: Buses are now Bus type (floating), only ground is fixed.

    auto gnd = make_device("gnd", "RefNode",
        {{"value", "0.0"}},
        {{"v", PortDirection::Out}});
    auto bus1 = make_device("bus1", "Bus", {{}},
        {{"v", PortDirection::Out}});
    auto bus2 = make_device("bus2", "Bus", {{}},
        {{"v", PortDirection::Out}});
    auto bat1 = make_device("bat1", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.01"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto bat2 = make_device("bat2", "Battery",
        {{"v_nominal", "24.0"}, {"internal_r", "0.01"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto control = make_device("ctrl", "RefNode", {{"value", "28.0"}}, {{"v", PortDirection::Out}});
    auto relay = make_device("relay", "Relay", {{}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"control", PortDirection::In}});
    auto load1 = make_device("load1", "Resistor",
        {{"conductance", "0.035"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto load2 = make_device("load2", "Resistor", {{"conductance", "0.035"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {
        gnd, bus1, bus2, bat1, bat2, control, relay, load1, load2
    };
    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat1.v_out", "bus1.v"},
        {"bat2.v_out", "bus2.v"},
        {"bus1.v", "relay.v_in"},
        {"relay.v_out", "bus2.v"},
        {"bus1.v", "load1.v_in"},
        {"bus2.v", "load2.v_in"},
        {"load1.v_out", "gnd.v"},
        {"load2.v_out", "gnd.v"},
        {"bat1.v_in", "gnd.v"},
        {"bat2.v_in", "gnd.v"},
        {"ctrl.v", "relay.control"},  // Control = 28V (relay held closed)
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);

    float v_bus1 = get_voltage(state, result, "bus1.v");
    float v_bus2 = get_voltage(state, result, "bus2.v");
    float v_gnd = get_voltage(state, result, "gnd.v");

    // Must not be NaN (was the original bug)
    EXPECT_FALSE(std::isnan(v_bus1)) << "bus1 voltage is NaN - SOR diverged";
    EXPECT_FALSE(std::isnan(v_bus2)) << "bus2 voltage is NaN - SOR diverged";
    EXPECT_FALSE(std::isinf(v_bus1)) << "bus1 voltage is Inf - SOR diverged";
    EXPECT_FALSE(std::isinf(v_bus2)) << "bus2 voltage is Inf - SOR diverged";

    EXPECT_FLOAT_EQ(v_gnd, 0.0f);

    // With closed relay, both buses should be at same voltage
    EXPECT_NEAR(v_bus1, v_bus2, 0.1f)
        << "Closed relay should equalize bus voltages";

    // Voltage should be between 24V and 28V
    EXPECT_GT(v_bus1, 24.0f);
    EXPECT_LT(v_bus1, 28.0f);
}

TEST(RegressionTest, GeneratorAndBatteryOnSameBus) {
    // Real-world scenario: generator (28.5V) and battery (28V) on same bus.
    // This was causing instability with RefNode buses.

    auto gnd = make_device("gnd", "RefNode",
        {{"value", "0.0"}},
        {{"v", PortDirection::Out}});
    auto bus = make_device("bus", "Bus", {{}},
        {{"v", PortDirection::Out}});
    auto bat = make_device("bat", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.01"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto gen = make_device("gen", "Generator",
        {{"v_nominal", "28.5"}, {"internal_r", "0.005"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto load = make_device("load", "Resistor",
        {{"conductance", "0.1"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, bus, bat, gen, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat.v_out", "bus.v"},
        {"gen.v_out", "bus.v"},
        {"bus.v", "load.v_in"},
        {"load.v_out", "gnd.v"},
        {"bat.v_in", "gnd.v"},
        {"gen.v_in", "gnd.v"},
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);

    float v_bus = get_voltage(state, result, "bus.v");
    float v_gnd = get_voltage(state, result, "gnd.v");

    EXPECT_FLOAT_EQ(v_gnd, 0.0f);
    EXPECT_FALSE(std::isnan(v_bus));
    EXPECT_FALSE(std::isinf(v_bus));

    // Bus voltage should be between 28V and 28.5V
    EXPECT_GT(v_bus, 28.0f);
    EXPECT_LT(v_bus, 28.5f);
}

TEST(SwitchTest, OpenSwitchBlocksCurrent) {
    // Battery → Switch (open) → Load → Ground
    // Open switch = no current
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto battery = make_device("battery", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto sw = make_device("sw", "Switch", {{"closed", "false"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"control", PortDirection::In}});
    auto control = make_device("ctrl", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto load = make_device("load", "Resistor", {{"conductance", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, battery, sw, control, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "sw.v_in"},
        {"sw.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
        {"ctrl.v", "sw.control"},  // Control = 0V (switch stays open)
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);

    float v_after_switch = get_voltage(state, result, "sw.v_out");

    // Open switch: 0V (no connection)
    EXPECT_NEAR(v_after_switch, 0.0f, 0.01f);
}

TEST(SwitchTest, ClosedSwitchConducts) {
    // Battery → Switch (closed) → Load → Ground
    // Closed switch = conducts like wire
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto battery = make_device("battery", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto sw = make_device("sw", "Switch", {{"closed", "true"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"control", PortDirection::In}});
    auto control = make_device("ctrl", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto load = make_device("load", "Resistor", {{"conductance", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, battery, sw, control, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "sw.v_in"},
        {"sw.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
        {"ctrl.v", "sw.control"},  // Control = 0V (switch stays closed by default)
    };

    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);

    float v_load = get_voltage(state, result, "load.v_in");

    // Closed switch: voltage reaches load (~28V through load)
    EXPECT_GT(v_load, 27.0f);
}

TEST(SwitchTest, ToggleOnControlEdge) {
    // Battery → Switch → Load
    // Control edge toggles switch state
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto battery = make_device("battery", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.01"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto sw = make_device("sw", "Switch", {{"closed", "false"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"control", PortDirection::In}});
    auto control_src = make_device("ctrl", "RefNode", {{"value", "28.0"}}, {{"v", PortDirection::Out}});
    auto load = make_device("load", "Resistor", {{"conductance", "0.1"}}, {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, battery, sw, control_src, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "sw.v_in"},
        {"sw.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
        {"ctrl.v", "sw.control"},  // Control = 28V
    };

    auto result = build_systems_dev(devices, connections);

    // Sanity check: system was built successfully
    EXPECT_GT(result.signal_count, 0u);
}

// =============================================================================
// Regression: Switch/Relay/HoldButton must drop v_out to 0 when open
// =============================================================================

TEST(RegressionTest, OpenSwitchDropsVoltageToZero) {
    // BUG: Switch turned off visually but v_out kept old voltage.
    // FIX: post_step forces v_out = 0 when open.
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto bat = make_device("bat", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.01"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto sw = make_device("sw", "Switch", {{"closed", "true"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out},
         {"control", PortDirection::In}, {"state", PortDirection::Out}});
    auto ctrl = make_device("ctrl", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto load = make_device("load", "Resistor", {{"conductance", "0.1"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, bat, sw, ctrl, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat.v_out", "sw.v_in"}, {"sw.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"}, {"bat.v_in", "gnd.v"},
        {"ctrl.v", "sw.control"},
    };

    // Phase 1: switch closed — voltage should reach load
    auto result = build_systems_dev(devices, connections);
    auto state = run_sor(result, devices, 200);
    float v_closed = get_voltage(state, result, "sw.v_out");
    EXPECT_GT(v_closed, 27.0f) << "Closed switch should pass voltage";

    // Phase 2: toggle switch open (change control signal)
    auto it_ctrl = result.port_to_signal.find("ctrl.v");
    state.across[it_ctrl->second] = 28.0f;  // trigger toggle
    for (int i = 0; i < 50; ++i) {
        state.clear_through();
        result.systems.solve_step(state, i);
        state.precompute_inv_conductance();
        for (size_t j = 0; j < state.across.size(); ++j) {
            if (!state.signal_types[j].is_fixed && state.inv_conductance[j] > 0.0f)
                state.across[j] += state.through[j] * state.inv_conductance[j] * 1.5f;
        }
        result.systems.post_step(state, 1.0f / 60.0f);
    }

    float v_open = get_voltage(state, result, "sw.v_out");
    EXPECT_NEAR(v_open, 0.0f, 0.01f) << "Open switch must drop v_out to 0";
}

TEST(RegressionTest, OpenRelayDropsVoltageToZero) {
    // BUG: Relay opened but v_out kept old voltage.
    // FIX: post_step forces v_out = 0 when open.
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto bat = make_device("bat", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.01"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    // Use Bus for control so we can change its value freely
    auto ctrl_bus = make_device("ctrl", "Bus", {{}}, {{"v", PortDirection::Out}});
    auto relay = make_device("relay", "Relay", {{}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}, {"control", PortDirection::In}});
    auto load = make_device("load", "Resistor", {{"conductance", "0.1"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, bat, ctrl_bus, relay, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat.v_out", "relay.v_in"}, {"relay.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"}, {"bat.v_in", "gnd.v"},
        {"ctrl.v", "relay.control"},
    };

    auto result = build_systems_dev(devices, connections);
    auto ctrl_idx = result.port_to_signal["ctrl.v"];

    // Phase 1: relay closed (control = 28V) — voltage passes
    auto state = run_sor(result, devices, 50);
    state.across[ctrl_idx] = 28.0f;  // high control → relay closed
    for (int i = 0; i < 100; ++i) {
        state.clear_through();
        result.systems.solve_step(state, i);
        state.precompute_inv_conductance();
        for (size_t j = 0; j < state.across.size(); ++j) {
            if (!state.signal_types[j].is_fixed && state.inv_conductance[j] > 0.0f)
                state.across[j] += state.through[j] * state.inv_conductance[j] * 1.5f;
        }
        state.across[ctrl_idx] = 28.0f;  // maintain control
        result.systems.post_step(state, 1.0f / 60.0f);
    }
    float v_closed = get_voltage(state, result, "relay.v_out");
    EXPECT_GT(v_closed, 27.0f) << "Closed relay should pass voltage";

    // Phase 2: drop control to 0 → relay opens
    for (int i = 0; i < 50; ++i) {
        state.clear_through();
        state.across[ctrl_idx] = 0.0f;  // low control → relay open
        result.systems.solve_step(state, i);
        state.precompute_inv_conductance();
        for (size_t j = 0; j < state.across.size(); ++j) {
            if (!state.signal_types[j].is_fixed && state.inv_conductance[j] > 0.0f)
                state.across[j] += state.through[j] * state.inv_conductance[j] * 1.5f;
        }
        state.across[ctrl_idx] = 0.0f;  // maintain control
        result.systems.post_step(state, 1.0f / 60.0f);
    }

    float v_open = get_voltage(state, result, "relay.v_out");
    EXPECT_NEAR(v_open, 0.0f, 0.01f) << "Open relay must drop v_out to 0";
}

TEST(RegressionTest, ReleasedHoldButtonDropsVoltageToZero) {
    // BUG: HoldButton released but v_out kept old voltage.
    // FIX: post_step forces v_out = 0 when released.
    auto gnd = make_device("gnd", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto bat = make_device("bat", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.01"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});
    auto ctrl = make_device("ctrl", "RefNode", {{"value", "0.0"}}, {{"v", PortDirection::Out}});
    auto btn = make_device("btn", "HoldButton", {{}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out},
         {"control", PortDirection::In}, {"state", PortDirection::Out}});
    auto load = make_device("load", "Resistor", {{"conductance", "0.1"}},
        {{"v_in", PortDirection::In}, {"v_out", PortDirection::Out}});

    std::vector<DeviceInstance> devices = {gnd, bat, ctrl, btn, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat.v_out", "btn.v_in"}, {"btn.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"}, {"bat.v_in", "gnd.v"},
        {"ctrl.v", "btn.control"},
    };

    auto result = build_systems_dev(devices, connections);

    // Phase 1: press button (control → 1.0V)
    auto it_ctrl = result.port_to_signal.find("ctrl.v");
    auto ctrl_idx = it_ctrl->second;

    auto state = run_sor(result, devices, 50);  // initial settle
    // Send press command
    state.across[ctrl_idx] = 1.0f;
    state.signal_types[ctrl_idx].is_fixed = false;
    for (int i = 0; i < 100; ++i) {
        state.clear_through();
        result.systems.solve_step(state, i);
        state.precompute_inv_conductance();
        for (size_t j = 0; j < state.across.size(); ++j) {
            if (!state.signal_types[j].is_fixed && state.inv_conductance[j] > 0.0f)
                state.across[j] += state.through[j] * state.inv_conductance[j] * 1.5f;
        }
        result.systems.post_step(state, 1.0f / 60.0f);
    }

    float v_pressed = get_voltage(state, result, "btn.v_out");
    EXPECT_GT(v_pressed, 27.0f) << "Pressed button should pass voltage";

    // Phase 2: release button (control → 2.0V)
    state.across[ctrl_idx] = 2.0f;
    for (int i = 0; i < 50; ++i) {
        state.clear_through();
        result.systems.solve_step(state, i);
        state.precompute_inv_conductance();
        for (size_t j = 0; j < state.across.size(); ++j) {
            if (!state.signal_types[j].is_fixed && state.inv_conductance[j] > 0.0f)
                state.across[j] += state.through[j] * state.inv_conductance[j] * 1.5f;
        }
        result.systems.post_step(state, 1.0f / 60.0f);
    }

    float v_released = get_voltage(state, result, "btn.v_out");
    EXPECT_NEAR(v_released, 0.0f, 0.01f) << "Released button must drop v_out to 0";
}
