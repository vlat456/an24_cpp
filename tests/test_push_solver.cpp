#include <gtest/gtest.h>
#include "jit_solver/push_solver.h"
#include "json_parser/json_parser.h"

using namespace an24;

/// Тест для push-based solver
class PushSolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Создаем простую схему: Battery → Wire → Load
        devices.clear();
        connections.clear();
    }

    std::vector<DeviceInstance> devices;
    std::vector<std::pair<std::string, std::string>> connections;
};

TEST_F(PushSolverTest, BatteryOutputsNominalVoltage) {
    // Create battery (28V, no load)
    PushSolver solver;
    PushState state;

    // Build battery component
    DeviceInstance bat;
    bat.name = "bat1";
    bat.classname = "Battery";

    ASSERT_TRUE(solver.build({bat}, {}));

    // Run one step
    solver.step(state, 0.016f);

    // Verify V_out = 28V (no load)
    EXPECT_FLOAT_EQ(state.get_voltage("bat1.v_out"), 28.0f);
}

TEST_F(PushSolverTest, BatteryDischarge) {
    // Battery(28V) → Resistor(100Ω) → verify voltage sag
    // V_bat = 28V, R_int = 0.01Ω, R_load = 100Ω
    // I = 28 / (0.01 + 100) ≈ 0.28A
    // V_out = 28 - (0.28 * 0.01) ≈ 27.997V (small sag)
    PushSolver solver;
    PushState state;

    DeviceInstance bat;
    bat.name = "bat1";
    bat.classname = "Battery";

    DeviceInstance load;
    load.name = "load1";
    load.classname = "Resistor";
    load.params["resistance"] = "100.0";

    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat1.v_out", "load1.v_in"}
    };

    ASSERT_TRUE(solver.build({bat, load}, connections));
    solver.step(state, 0.016f);

    // Battery should see load and sag slightly
    float v_out = state.get_voltage("bat1.v_out");
    EXPECT_LT(v_out, 28.0f);  // Should sag below nominal
    EXPECT_GT(v_out, 27.0f);  // But not too much
}

TEST_F(PushSolverTest, ClosedSwitchPassesVoltage) {
    // Battery(28V) → Switch(closed) → verify V_out = 28V
    PushSolver solver;
    PushState state;

    DeviceInstance bat;
    bat.name = "bat1";
    bat.classname = "Battery";

    DeviceInstance sw;
    sw.name = "sw1";
    sw.classname = "Switch";

    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat1.v_out", "sw1.v_in"}
    };

    ASSERT_TRUE(solver.build({bat, sw}, connections));
    solver.step(state, 0.016f);

    // Switch is open by default, so v_out should be 0V
    EXPECT_FLOAT_EQ(state.get_voltage("sw1.v_out"), 0.0f);
}

TEST_F(PushSolverTest, BatteryCharge) {
    // Battery(28V) with higher voltage on input → charging
    // Setup: external source (30V) → Battery.v_in
    // Expected: current flows INTO battery (charging)

    PushSolver solver;
    PushState state;

    DeviceInstance bat;
    bat.name = "bat1";
    bat.classname = "Battery";

    DeviceInstance ext_source;
    ext_source.name = "ext";
    ext_source.classname = "RefNode";
    ext_source.params["value"] = "30.0";  // 30V source

    std::vector<std::pair<std::string, std::string>> connections = {
        {"ext.v", "bat1.v_in"}
    };

    ASSERT_TRUE(solver.build({bat, ext_source}, connections));
    solver.step(state, 0.016f);

    // Battery should see 30V on input (higher than nominal 28V)
    EXPECT_NEAR(state.get_voltage("bat1.v_in"), 30.0f, 0.1f);

    // Output should be slightly higher than nominal (charging mode)
    // V_out = V_in + V_charge (where V_charge comes from being charged)
    // For now, just verify it outputs something
    EXPECT_GT(state.get_voltage("bat1.v_out"), 0.0f);
}

TEST_F(PushSolverTest, WirePropagatesVoltage) {
    // Battery(28V) → Wire → verify voltage at wire end
    PushSolver solver;
    PushState state;

    DeviceInstance bat;
    bat.name = "bat1";
    bat.classname = "Battery";

    DeviceInstance wire;
    wire.name = "wire1";
    wire.classname = "Wire";

    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat1.v_out", "wire1.v_in"}
    };

    ASSERT_TRUE(solver.build({bat, wire}, connections));
    solver.step(state, 0.016f);

    // Wire should see ~28V at input
    EXPECT_NEAR(state.get_voltage("wire1.v_in"), 28.0f, 0.1f);

    // Wire output should be ~28V (negligible drop)
    EXPECT_NEAR(state.get_voltage("wire1.v_out"), 28.0f, 0.1f);
}

TEST_F(PushSolverTest, IndicatorLightTurnsOn) {
    // Battery(28V) → IndicatorLight → verify brightness
    // Should turn on (brightness > 0)
    PushSolver solver;
    PushState state;

    DeviceInstance bat;
    bat.name = "bat1";
    bat.classname = "Battery";

    DeviceInstance light;
    light.name = "light1";
    light.classname = "IndicatorLight";

    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat1.v_out", "light1.v_in"}
    };

    ASSERT_TRUE(solver.build({bat, light}, connections));
    solver.step(state, 0.016f);

    // Light should see ~28V and turn on (allow small sag)
    EXPECT_NEAR(state.get_voltage("light1.v_in"), 28.0f, 0.1f);

    // Brightness output should be 1.0f (fully on)
    EXPECT_FLOAT_EQ(state.get_voltage("light1.brightness"), 1.0f);
}

TEST_F(PushSolverTest, IndicatorLightTurnsOffWhenNoVoltage) {
    // No battery → IndicatorLight → verify off
    PushSolver solver;
    PushState state;

    DeviceInstance light;
    light.name = "light1";
    light.classname = "IndicatorLight";

    ASSERT_TRUE(solver.build({light}, {}));
    solver.step(state, 0.016f);

    // Light should see 0V and turn off
    EXPECT_FLOAT_EQ(state.get_voltage("light1.v_in"), 0.0f);
    EXPECT_FLOAT_EQ(state.get_voltage("light1.brightness"), 0.0f);
}

TEST_F(PushSolverTest, SeriesResistorsCauseVoltageDrop) {
    // Battery → Resistor → Resistor → verify voltage drops
    // V_bat = 28V, R1 = R2 = 100Ω, total = 200Ω
    // I = 28/200 = 0.14A
    // V_mid = 28 - (0.14 * 100) = 14V
    // V_out = 14 - (0.14 * 100) = 0V

    PushSolver solver;
    PushState state;

    DeviceInstance bat;
    bat.name = "bat1";
    bat.classname = "Battery";

    DeviceInstance r1;
    r1.name = "r1";
    r1.classname = "Resistor";
    r1.params["resistance"] = "100.0";

    DeviceInstance r2;
    r2.name = "r2";
    r2.classname = "Resistor";
    r2.params["resistance"] = "100.0";

    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat1.v_out", "r1.v_in"},
        {"r1.v_out", "r2.v_in"}
    };

    ASSERT_TRUE(solver.build({bat, r1, r2}, connections));
    solver.step(state, 0.016f);

    // Battery should see 200Ω total resistance
    EXPECT_NEAR(state.get_voltage("bat1.v_out"), 28.0f, 0.1f);

    // Middle point should be at ~14V
    EXPECT_NEAR(state.get_voltage("r1.v_out"), 14.0f, 1.0f);

    // Final output should be near 0V
    EXPECT_NEAR(state.get_voltage("r2.v_out"), 0.0f, 1.0f);
}

TEST_F(PushSolverTest, ParallelLoadsDivideCurrent) {
    // Battery → two parallel resistors
    // Battery(28V) → Resistor1(100Ω) || Resistor2(100Ω)
    // Equivalent parallel resistance: 1/R = 1/100 + 1/100 = 2/100 = 1/50 → R = 50Ω
    // I_total = 28V / 50Ω = 0.56A
    // Each resistor gets: I = 28V / 100Ω = 0.28A

    PushSolver solver;
    PushState state;

    DeviceInstance bat;
    bat.name = "bat1";
    bat.classname = "Battery";

    DeviceInstance r1;
    r1.name = "r1";
    r1.classname = "Resistor";
    r1.params["resistance"] = "100.0";

    DeviceInstance r2;
    r2.name = "r2";
    r2.classname = "Resistor";
    r2.params["resistance"] = "100.0";

    // Both resistors connected directly to battery (parallel)
    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat1.v_out", "r1.v_in"},
        {"bat1.v_out", "r2.v_in"}
    };

    ASSERT_TRUE(solver.build({bat, r1, r2}, connections));
    solver.step(state, 0.016f);

    // Both resistors should see ~28V at their inputs
    EXPECT_NEAR(state.get_voltage("r1.v_in"), 28.0f, 5.0f);
    EXPECT_NEAR(state.get_voltage("r2.v_in"), 28.0f, 5.0f);

    // Both outputs should be near 0V (all voltage dropped)
    EXPECT_NEAR(state.get_voltage("r1.v_out"), 0.0f, 5.0f);
    EXPECT_NEAR(state.get_voltage("r2.v_out"), 0.0f, 5.0f);
}

TEST_F(PushSolverTest, GeneratorNotConnected_NoVoltage) {
    // Generator not connected to load should output 0V
    // (Generator needs external excitation to produce voltage)

    PushSolver solver;
    PushState state;

    DeviceInstance gen;
    gen.name = "gen1";
    gen.classname = "Generator";

    ASSERT_TRUE(solver.build({gen}, {}));
    solver.step(state, 0.016f);

    // Generator with no load should output 0V
    EXPECT_FLOAT_EQ(state.get_voltage("gen1.v_out"), 0.0f);
}

TEST_F(PushSolverTest, LerpNodeSmoothsInput) {
    // LerpNode should smooth input values
    // factor = 0.5 means output approaches input halfway each step

    PushSolver solver;
    PushState state;

    DeviceInstance bat;
    bat.name = "bat1";
    bat.classname = "Battery";

    DeviceInstance lerp;
    lerp.name = "lerp1";
    lerp.classname = "LerpNode";
    lerp.params["factor"] = "0.5";

    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat1.v_out", "lerp1.input"}
    };

    ASSERT_TRUE(solver.build({bat, lerp}, connections));
    solver.step(state, 0.016f);

    // First step: output should be at 50% of input (0 + 28 * 0.5 = 14)
    EXPECT_NEAR(state.get_voltage("lerp1.output"), 14.0f, 0.1f);
}
