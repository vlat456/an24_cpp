#include <gtest/gtest.h>
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/state.h"
#include "core/solvers/jit/components/all.h"
#include "json_parser/json_parser.h"
#include "parse_number.h"
#include "test_execution_phases.h"
#include "jit_build_input_test_helper.h"


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
    dev.spec = load_component_registry("library/").get(classname);
    return dev;
}

// =============================================================================
// Helper: run simulation to steady state (push model)
// =============================================================================
static SimulationState run_simulation(
    BuildResult& result,
    const std::vector<ResolvedDevice>& devices,
    int steps = 50
) {
    SimulationState state;

    // Allocate signals
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(), result.fixed_signals.end(), i);
        state.allocate_signal(0.0f);
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
// Integration Tests - structural bridge and port behavior
// =============================================================================



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
    ComponentSpec bus_spec = PrimitiveSpec{};
    auto* prim = as_primitive_mut(bus_spec);
    ASSERT_NE(prim, nullptr);
    prim->classname = "Bus";
    prim->domains = {Domain::Electrical};
    prim->execution = ExecutionPhases{.electrical_passive = true};

    DeviceInstance dev;
    dev.name = "test_dev";
    dev.classname = "Bus";
    dev.priority = "med";
    dev.critical = false;

    dev.ports["i"]  = Port{bp2::Direction::Input,  PortType::Any};
    dev.ports["o1"] = Port{bp2::Direction::Output, PortType::Any, std::string("i")};  // alias → "i"
    dev.ports["o2"] = Port{bp2::Direction::Output, PortType::Any};
    dev = resolve_device(dev, bus_spec);

    std::vector<DeviceInstance> devices = { dev };
    std::vector<std::vector<std::string>> signal_groups = {
        {"test_dev.i", "test_dev.o1"},
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

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
