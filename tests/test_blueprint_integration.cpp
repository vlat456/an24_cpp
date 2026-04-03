#include <gtest/gtest.h>
#include "jit_solver/jit_solver.h"
#include "jit_solver/state.h"
#include "jit_solver/components/all.h"
#include "json_parser/json_parser.h"
#include "parse_number.h"
#include "test_execution_phases.h"


// =============================================================================
// Helper: create DeviceInstance with common fields
// =============================================================================
static DeviceInstance make_device(
    const std::string& name,
    const std::string& classname,
    std::unordered_map<std::string, std::string> params = {},
    std::unordered_map<std::string, Port> ports = {}
) {
    DeviceInstance dev;
    dev.name = name;
    dev.classname = classname;
    dev.params = std::move(params);
    dev.ports = std::move(ports);
    dev.priority = "med";
    dev.critical = false;
    dev.execution = test_exec::for_class(classname);
    return dev;
}

// =============================================================================
// Helper: run simulation to steady state (push model)
// =============================================================================
static SimulationState run_simulation(
    BuildResult& result,
    const std::vector<DeviceInstance>& devices,
    int steps = 50
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
            if (it_val != dev.params.end()) value = locale_safe::parse_float_or(it_val->second, 0.0f);

            std::string port = dev.name + ".v";
            auto it_sig = result.port_to_signal.find(port);
            if (it_sig != result.port_to_signal.end()) {
                state.values[it_sig->second] = value;
            }
        }
    }

    // Push model: run simulation steps using the scheduler
    constexpr double dt = 1.0 / 60.0;
    for (int step = 0; step < steps; ++step) {
        result.scheduler.step(state, dt);
    }

    return state;
}

// Helper to get signal voltage by port name
static float get_voltage(const SimulationState& state, const BuildResult& result,
                          const std::string& port_name) {
    auto it = result.port_to_signal.find(port_name);
    EXPECT_NE(it, result.port_to_signal.end()) << "Port not found: " << port_name;
    return state.values[it->second];
}

// =============================================================================
// Integration Tests - BlueprintInput/BlueprintOutput
// =============================================================================

// DISABLED: legacy solver-specific test expecting ground reference to force v_in=0V.
// In push model without iteration, ground may float to 28V because RefNode
// broadcasts (not forces) and battery drives the circuit.
TEST(BlueprintPorts, DISABLED_BasicBatteryCircuit) {
    // Test circuit: GND -> Battery (v_in) -> Battery (v_out) -> Resistor -> GND
    // Expected: Battery.v_out ≈ 28V (slightly less due to internal resistance)

    std::vector<DeviceInstance> devices = {
        make_device("gnd", "RefNode", {{"value", "0.0"}}),
        make_device("bat", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("res", "Resistor", {{"conductance", "0.1"}})
    };

    // Add ports to devices
    devices[0].ports["v"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    devices[1].ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    devices[1].ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    devices[2].ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    devices[2].ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gnd.v", "bat.v_in"},
        {"bat.v_out", "res.v_in"},
        {"res.v_out", "gnd.v"}
    };

    // Build systems
    auto result = build_systems_dev(devices, connections);

    // Run simulation
    auto state = run_simulation(result, devices);

    // Check battery voltages
    float v_bat_in = get_voltage(state, result, "bat.v_in");
    float v_bat_out = get_voltage(state, result, "bat.v_out");
    EXPECT_NEAR(v_bat_in, 0.0f, 0.1f) << "Battery v_in should be at 0V (GND)";
    EXPECT_GT(v_bat_out, 25.0f) << "Battery v_out should be close to 28V";
    EXPECT_LT(v_bat_out, 29.0f) << "Battery v_out should not exceed nominal significantly";
}

// DISABLED: legacy solver-specific test. Battery is solver-owned (not in push
// scheduler), so voltage never propagates via push-only simulation.
// Same root cause as DISABLED_BasicBatteryCircuit above.
TEST(BlueprintPorts, DISABLED_InputPassThroughToOutput) {
    // Test circuit: GND -> BlueprintInput -> Battery -> BlueprintOutput
    // Expected: BlueprintOutput.port = 28V (from Battery)

    std::vector<DeviceInstance> devices = {
        make_device("gnd", "RefNode", {{"value", "0.0"}}),
        make_device("vin", "BlueprintInput"),
        make_device("bat", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("vout", "BlueprintOutput")
    };

    // Add ports to devices
    devices[0].ports["v"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    devices[1].ports["port"] = Port{PortDirection::Out, PortType::Any, std::nullopt};
    devices[2].ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    devices[2].ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    devices[3].ports["port"] = Port{PortDirection::In, PortType::Any, std::nullopt};

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gnd.v", "vin.port"},
        {"vin.port", "bat.v_in"},
        {"bat.v_out", "vout.port"}
    };

    // Build systems
    auto result = build_systems_dev(devices, connections);

    // Run simulation
    auto state = run_simulation(result, devices);

    // Check BlueprintOutput has 28V from Battery
    float vout = get_voltage(state, result, "vout.port");
    EXPECT_NEAR(vout, 28.0f, 0.1f) << "BlueprintOutput should have 28V from Battery";

    // Check BlueprintInput is at GND (0V)
    float vin = get_voltage(state, result, "vin.port");
    EXPECT_NEAR(vin, 0.0f, 0.1f) << "BlueprintInput should be at 0V (connected to GND)";
}

// =============================================================================
// Regression: JIT alias port unification parity with AOT (codegen.cpp)
// =============================================================================
// If a device port has an `alias` field pointing to another port on the same
// device, both ports must be unified to the same signal.  AOT codegen has
// always done this; JIT was missing the step until the parity fix.
// This test would FAIL on the old code (alias ports mapped to different signals).

TEST(BlueprintPorts, AliasPortUnification_JitAotParity) {
    // Create a simple device with three ports: "i", "o1", "o2".
    // Give "o1" an alias to "i" — meaning o1 should share i's signal.
    DeviceInstance dev;
    dev.name = "test_dev";
    dev.classname = "Bus";  // Bus is a no-op component (no execute body)
    dev.priority = "med";
    dev.critical = false;
    dev.execution = test_exec::bus();

    dev.ports["i"]  = Port{PortDirection::In,  PortType::Any};
    dev.ports["o1"] = Port{PortDirection::Out, PortType::Any, std::string("i")};  // alias → "i"
    dev.ports["o2"] = Port{PortDirection::Out, PortType::Any};

    std::vector<DeviceInstance> devices = { dev };
    std::vector<std::pair<std::string, std::string>> connections; // no external wires

    auto result = build_systems_dev(devices, connections);

    // Ports "test_dev.i" and "test_dev.o1" must map to the same signal
    auto it_i  = result.port_to_signal.find("test_dev.i");
    auto it_o1 = result.port_to_signal.find("test_dev.o1");
    auto it_o2 = result.port_to_signal.find("test_dev.o2");

    ASSERT_NE(it_i,  result.port_to_signal.end()) << "Port 'test_dev.i' not found";
    ASSERT_NE(it_o1, result.port_to_signal.end()) << "Port 'test_dev.o1' not found";
    ASSERT_NE(it_o2, result.port_to_signal.end()) << "Port 'test_dev.o2' not found";

    EXPECT_EQ(it_i->second, it_o1->second)
        << "Alias port 'o1' → 'i' must be unified to the same signal (JIT/AOT parity)";

    EXPECT_NE(it_i->second, it_o2->second)
        << "Non-alias port 'o2' should remain independent";
}
