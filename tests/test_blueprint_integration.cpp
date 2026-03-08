#include <gtest/gtest.h>
#include "jit_solver/jit_solver.h"
#include "jit_solver/state.h"
#include "jit_solver/SOR_constants.h"
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
    std::unordered_map<std::string, Port> ports = {}
) {
    DeviceInstance dev;
    dev.name = name;
    dev.classname = classname;
    dev.params = std::move(params);
    dev.ports = std::move(ports);
    dev.priority = "med";
    dev.critical = false;
    return dev;
}

// =============================================================================
// Helper: run simulation to steady state
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

        // Solve electrical domain using domain_components (like SimulationController)
        for (auto* variant : result.domain_components.electrical) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_electrical(state, 0.0f); }) {
                    comp.solve_electrical(state, 1.0f / 60.0f);
                }
            }, *variant);
        }

        state.precompute_inv_conductance();

        for (size_t i = 0; i < state.across.size(); ++i) {
            if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
                state.across[i] += state.through[i] * state.inv_conductance[i] * SOR::OMEGA;
            }
        }
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
// Integration Tests - BlueprintInput/BlueprintOutput
// =============================================================================

TEST(BlueprintPorts, BasicBatteryCircuit) {
    // Test circuit: GND -> Battery (v_in) -> Battery (v_out) -> Resistor -> GND
    // Expected: Battery.v_out ≈ 28V (slightly less due to internal resistance)

    std::vector<DeviceInstance> devices = {
        make_device("gnd", "RefNode", {{"value", "0.0"}}),
        make_device("bat", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.01"}}),
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

TEST(BlueprintPorts, InputPassThroughToOutput) {
    // Test circuit: GND -> BlueprintInput -> Battery -> BlueprintOutput
    // Expected: BlueprintOutput.port = 28V (from Battery)

    std::vector<DeviceInstance> devices = {
        make_device("gnd", "RefNode", {{"value", "0.0"}}),
        make_device("vin", "BlueprintInput"),
        make_device("bat", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.01"}}),
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
