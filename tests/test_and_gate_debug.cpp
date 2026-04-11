/// Debug test for AND gate always outputting 1
/// Reproduces the circuit from a local blueprint document save:
///   Battery -> Bus -> Positive_V_to_Bool -> AND.A
///   HoldButton.state -> AND.B
///   (HoldButton not pressed -> state = 0 -> AND should output 0)

#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/components/all.h"
#include "parse_number.h"
#include <spdlog/spdlog.h>
#include <cstdio>

class ANDGateDebugTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::debug);
    }
};

TEST_F(ANDGateDebugTest, AND_With_Battery_VToBool_HoldButton) {
    // Minimal circuit reproducing the blueprint:
    //   gnd(0V) -> bat.v_in
    //   bat.v_out -> bus.v
    //   bus.v -> v2b.Vin
    //   v2b.o -> and_1.A
    //   hb.state -> and_1.B
    //   (hb.control is unconnected - floats at 0)
    //   (hb.v_in -> bus.v, hb.v_out unconnected)

    const char* json = R"({
        "devices": [
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
            {"name": "bat", "classname": "ElectricalSource", "params": {
                "voltage": "28.0", "resistance": "0.01"
            }},
            {"name": "bus", "classname": "Bus"},
            {"name": "v2b", "classname": "Positive_V_to_Bool"},
            {"name": "hb", "classname": "HoldButton", "params": {"idle": "0.0"}},
            {"name": "and_1", "classname": "AND"}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "bus.v"},
            {"from": "bus.v", "to": "v2b.Vin"},
            {"from": "bus.v", "to": "hb.v_in"},
            {"from": "v2b.o", "to": "and_1.A"},
            {"from": "hb.state", "to": "and_1.B"}
        ]
    })";

    // Build
    JitBuildInput input = build_input_from_json(json);
    BuildResult result = build_systems_dev(input);

    // Allocate state
    SimulationState state;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(), result.fixed_signals.end(), i);
        state.allocate_signal(0.0f);
    }

    // Initialize RefNodes
    for (const auto& dev : input.devices) {
        if (dev.classname == "RefNode") {
            float value = locale_safe::parse_float_or(dev.params.at("value"), 0.0f);
            auto it = result.port_to_signal.find(dev.name + ".v");
            if (it != result.port_to_signal.end()) {
                state.values[it->second] = value;
            }
        }
    }

    // Seed battery output voltage: Battery is solver-owned — its voltage is
    // produced by solve_electrical(), which this test intentionally skips to
    // isolate port mapping / logical gate wiring. Seeding bat.v_out to 28V
    // is equivalent to what the electrical solver would produce.
    {
        auto it = result.port_to_signal.find("bat.v_out");
        ASSERT_NE(it, result.port_to_signal.end()) << "bat.v_out must exist";
        state.values[it->second] = 28.0f;
    }

    // Print signal mapping
    printf("\n=== SIGNAL MAP ===\n");
    for (auto& [port, sig] : result.port_to_signal) {
        printf("  %-30s -> signal[%u]\n", port.c_str(), sig);
    }

    // Get signal indices for the signals we care about
    auto get_sig = [&](const std::string& port) -> uint32_t {
        auto it = result.port_to_signal.find(port);
        EXPECT_NE(it, result.port_to_signal.end()) << "Missing: " << port;
        return it->second;
    };

    uint32_t sig_bus_v    = get_sig("bus.v");
    uint32_t sig_v2b_Vin  = get_sig("v2b.Vin");
    uint32_t sig_v2b_o    = get_sig("v2b.o");
    uint32_t sig_hb_state = get_sig("hb.state");
    uint32_t sig_and_A    = get_sig("and_1.A");
    uint32_t sig_and_B    = get_sig("and_1.B");
    uint32_t sig_and_o    = get_sig("and_1.o");

    printf("\n=== KEY SIGNALS ===\n");
    printf("  bus.v     = signal[%u]\n", sig_bus_v);
    printf("  v2b.Vin   = signal[%u] (should == bus.v)\n", sig_v2b_Vin);
    printf("  v2b.o     = signal[%u]\n", sig_v2b_o);
    printf("  hb.state  = signal[%u]\n", sig_hb_state);
    printf("  and_1.A   = signal[%u] (should == v2b.o)\n", sig_and_A);
    printf("  and_1.B   = signal[%u] (should == hb.state)\n", sig_and_B);
    printf("  and_1.o   = signal[%u]\n", sig_and_o);

    // Verify wired signals share the same index
    EXPECT_EQ(sig_v2b_Vin, sig_bus_v) << "v2b.Vin should be wired to bus.v";
    EXPECT_EQ(sig_and_A, sig_v2b_o) << "and_1.A should be wired to v2b.o";
    EXPECT_EQ(sig_and_B, sig_hb_state) << "and_1.B should be wired to hb.state";

    double dt = 1.0 / 60.0;

    // Run simulation steps
    printf("\n=== SIMULATION ===\n");
    for (int step = 0; step < 5; ++step) {
        result.scheduler.step(state, dt);
        printf("  step %d: bus.v=%.4f v2b.o=%.4f hb.state=%.4f and.A=%.4f and.B=%.4f and.o=%.4f\n",
               step, state.values[sig_bus_v], state.values[sig_v2b_o], state.values[sig_hb_state],
               state.values[sig_and_A], state.values[sig_and_B], state.values[sig_and_o]);
    }

    // After 5 steps:
    // - Battery should output ~28V on bus
    // - Positive_V_to_Bool should convert to 1.0 (28V > 0)
    // - HoldButton not pressed -> state = 0.0
    // - AND: A=1.0, B=0.0 -> output should be 0.0
    printf("\n=== ASSERTIONS ===\n");
    float bus_v = state.values[sig_bus_v];
    float and_a = state.values[sig_and_A];
    float and_b = state.values[sig_and_B];
    float and_o = state.values[sig_and_o];

    printf("  bus.v = %.4f (expect ~28V)\n", bus_v);
    printf("  AND.A = %.4f (expect 1.0 from V_to_Bool)\n", and_a);
    printf("  AND.B = %.4f (expect 0.0 from HoldButton.state)\n", and_b);
    printf("  AND.o = %.4f (expect 0.0 since B is false)\n", and_o);

    EXPECT_GT(bus_v, 20.0f) << "Battery should charge bus";
    EXPECT_NEAR(and_a, 1.0f, 0.01f) << "V_to_Bool should output 1.0 (bus has voltage)";
    EXPECT_NEAR(and_b, 0.0f, 0.01f) << "HoldButton not pressed, state should be 0.0";
    EXPECT_NEAR(and_o, 0.0f, 0.01f) << "AND should output 0.0 (B is false)";
}
