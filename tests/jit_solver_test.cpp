#include <gtest/gtest.h>
#include "jit_solver/jit_solver.h"
#include "jit_solver/state.h"
#include "jit_solver/components/all.h"

using namespace an24;

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
        if (dev.internal == "RefNode") {
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

    EXPECT_FLOAT_EQ(state.inv_conductance[0], 1.0f);    // fixed: always 1
    EXPECT_FLOAT_EQ(state.inv_conductance[1], 0.25f);   // 1/4
    EXPECT_FLOAT_EQ(state.inv_conductance[2], 0.0f);    // open circuit
}

// =============================================================================
// Unit Tests - build_systems_dev
// =============================================================================

TEST(BuildSystemsTest, BasicBuildSingleDevice) {
    DeviceInstance battery;
    battery.name = "bat_main_1";
    battery.internal = "Battery";
    battery.params["v_nominal"] = "28.0";
    battery.ports["v_in"] = "input";
    battery.ports["v_out"] = "output";

    std::vector<DeviceInstance> devices = {battery};
    std::vector<std::pair<std::string, std::string>> connections;

    auto result = build_systems_dev(devices, connections);

    EXPECT_GT(result.signal_count, 0u);
    EXPECT_EQ(result.systems.component_count(), 1u);
}

TEST(BuildSystemsTest, ConnectionsMergeSignals) {
    // Two devices connected: their shared ports should map to the same signal
    DeviceInstance battery{"bat", "Battery", {{"v_nominal", "28.0"}},
                           {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance relay{"relay", "Relay", {},
                         {{"v_in", "i"}, {"v_out", "o"}}};

    std::vector<DeviceInstance> devices = {battery, relay};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat.v_out", "relay.v_in"},
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
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};

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
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance dc_bus{"dc_bus", "Bus", {}, {{"v", "bus"}}};
    DeviceInstance battery{"battery", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance light1{"light1", "IndicatorLight", {{"max_brightness", "100.0"}},
        {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};
    DeviceInstance light2{"light2", "IndicatorLight", {{"max_brightness", "100.0"}},
        {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};

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
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance dc_bus{"dc_bus", "Bus", {}, {{"v", "bus"}}};
    DeviceInstance battery{"battery", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance light1{"light1", "IndicatorLight", {{"max_brightness", "100.0"}},
        {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};
    DeviceInstance light2{"light2", "IndicatorLight", {{"max_brightness", "100.0"}},
        {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};

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
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance battery{"battery", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.001"}},  // very low R
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance r1{"r1", "Resistor", {{"conductance", "1.0"}},
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance r2{"r2", "Resistor", {{"conductance", "1.0"}},
        {{"v_in", "i"}, {"v_out", "o"}}};

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
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance bus{"bus", "RefNode", {{"value", "28.0"}}, {{"v", "b"}}};
    DeviceInstance light{"light", "IndicatorLight", {{"max_brightness", "100.0"}},
        {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};

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
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance battery{"battery", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance load{"load", "Resistor", {{"conductance", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};

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
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance gen{"gen", "Generator",
        {{"v_nominal", "28.5"}, {"internal_r", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance load{"load", "Resistor", {{"conductance", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};

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
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance dc_bus{"dc_bus", "Bus", {}, {{"v", "bus"}}};
    DeviceInstance battery{"battery", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance light1{"light1", "IndicatorLight", {{"max_brightness", "100.0"}},
        {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};
    DeviceInstance light2{"light2", "IndicatorLight", {{"max_brightness", "100.0"}},
        {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};

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
    // Battery → Relay (closed, perfect switch) → Load (10Ω) → Ground
    // Closed relay is a zero-resistance switch: V_bus = V_after_relay.
    // V = 28 * 10 / (0.1 + 10) = 27.72V
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance battery{"battery", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance relay{"relay", "Relay", {},
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance load{"load", "Resistor", {{"conductance", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};

    std::vector<DeviceInstance> devices = {gnd, battery, relay, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "relay.v_in"},
        {"relay.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
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
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance battery{"battery", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance relay{"relay", "Relay", {{"closed", "false"}},
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance load{"load", "Resistor", {{"conductance", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};

    std::vector<DeviceInstance> devices = {gnd, battery, relay, load};
    std::vector<std::pair<std::string, std::string>> connections = {
        {"battery.v_out", "relay.v_in"},
        {"relay.v_out", "load.v_in"},
        {"load.v_out", "gnd.v"},
        {"battery.v_in", "gnd.v"},
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
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance bat1{"bat1", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.1"}},
        {{"v_in", "gi"}, {"v_out", "bo"}}};
    DeviceInstance bat2{"bat2", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.1"}},
        {{"v_in", "gi"}, {"v_out", "bo"}}};
    DeviceInstance load{"load", "Resistor", {{"conductance", "0.1"}},
        {{"v_in", "li"}, {"v_out", "lo"}}};

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
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance battery{"battery", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance l1{"l1", "IndicatorLight", {{"max_brightness", "100.0"}},
        {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};
    DeviceInstance l2{"l2", "IndicatorLight", {{"max_brightness", "50.0"}},
        {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};
    DeviceInstance l3{"l3", "IndicatorLight", {{"max_brightness", "200.0"}},
        {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};

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
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance battery{"battery", "Battery",
        {{"v_nominal", "28.0"}, {"internal_r", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance load{"load", "Resistor", {{"conductance", "0.1"}},
        {{"v_in", "i"}, {"v_out", "o"}}};

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
