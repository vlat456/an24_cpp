#include <gtest/gtest.h>
#include "editor/simulation.h"
#include "jit_solver/simulator.h"
#include "editor/persist.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"

/// Simulation integration tests

/// Helper to create a simple test circuit: gnd → battery → resistor → gnd
static Blueprint create_simple_circuit() {
    Blueprint bp;
    bp.grid_step = 16.0f;

    // Ground reference
    Node gnd;
    gnd.id = "gnd";
    gnd.name = "Ground";
    gnd.type_name = "RefNode";
    gnd.kind = NodeKind::Ref;
    gnd.output("v");
    gnd.at(80, 240);
    gnd.size_wh(40, 40);
    gnd.node_content.type = NodeContentType::Value;
    gnd.node_content.value = 0.0f;
    bp.add_node(std::move(gnd));

    // Battery
    Node batt;
    batt.id = "bat";
    batt.name = "Battery";
    batt.type_name = "Battery";
    batt.kind = NodeKind::Node;
    batt.input("v_in");
    batt.output("v_out");
    batt.at(80, 80);
    batt.size_wh(120, 80);
    bp.add_node(std::move(batt));

    // Resistor
    Node res;
    res.id = "res";
    res.name = "Resistor";
    res.type_name = "Resistor";
    res.kind = NodeKind::Node;
    res.input("v_in");
    res.output("v_out");
    res.at(320, 80);
    res.size_wh(120, 80);
    bp.add_node(std::move(res));

    // Ground wire: gnd.v -> bat.v_in
    Wire w1;
    w1.start.node_id = "gnd";
    w1.start.port_name = "v";
    w1.end.node_id = "bat";
    w1.end.port_name = "v_in";
    bp.add_wire(std::move(w1));

    // Battery to resistor: bat.v_out -> res.v_in
    Wire w2;
    w2.start.node_id = "bat";
    w2.start.port_name = "v_out";
    w2.end.node_id = "res";
    w2.end.port_name = "v_in";
    bp.add_wire(std::move(w2));

    // Resistor to ground: res.v_out -> gnd.v
    Wire w3;
    w3.start.node_id = "res";
    w3.start.port_name = "v_out";
    w3.end.node_id = "gnd";
    w3.end.port_name = "v";
    bp.add_wire(std::move(w3));

    return bp;
}

// ─── Build & basic lifecycle ───

TEST(SimulationTest, BuildsFromBlueprint) {
    Blueprint bp = create_simple_circuit();
    SimulationController sim;
    sim.build(bp);

    ASSERT_TRUE(sim.build_result.has_value());
    EXPECT_GE(sim.build_result->signal_count, 2u); // at least gnd + battery
}

TEST(SimulationTest, StepCounterIncrements) {
    Blueprint bp = create_simple_circuit();
    SimulationController sim;
    sim.build(bp);

    EXPECT_EQ(sim.step_count, 0u);
    sim.step(0.016f);
    EXPECT_EQ(sim.step_count, 1u);
    sim.step(0.016f);
    EXPECT_EQ(sim.step_count, 2u);
}

TEST(SimulationTest, RunningFlag) {
    SimulationController sim;
    EXPECT_FALSE(sim.running);

    Blueprint bp = create_simple_circuit();
    sim.build(bp);
    sim.start();
    EXPECT_TRUE(sim.running);

    sim.step(0.016f);
    EXPECT_GT(sim.time, 0.0f);

    sim.stop();
    EXPECT_FALSE(sim.running);
}

TEST(SimulationTest, ResetClearsState) {
    Blueprint bp = create_simple_circuit();
    SimulationController sim;

    sim.build(bp);

    // Run simulation to get some non-zero values
    for (int i = 0; i < 50; i++) sim.step(0.016f);

    // Check that simulation has progressed
    EXPECT_GT(sim.time, 0.0f);
    EXPECT_GT(sim.step_count, 0u);

    // Get voltage at battery port (should be non-zero after running)
    float v_running = sim.get_port_value("battery_1", "v_out");

    // Reset simulation
    sim.reset();

    // Check that state is cleared
    EXPECT_FALSE(sim.running);
    EXPECT_EQ(sim.time, 0.0f);
    EXPECT_EQ(sim.step_count, 0u);

    // All voltages should be 0 after reset
    float v_reset = sim.get_port_value("battery_1", "v_out");
    EXPECT_FLOAT_EQ(v_reset, 0.0f);
}

// ─── State reset on rebuild ───

TEST(SimulationTest, RebuildResetsState) {
    Blueprint bp = create_simple_circuit();
    SimulationController sim;

    // First build
    sim.build(bp);
    size_t signals_first = sim.state.across.size();

    // Run a few steps
    for (int i = 0; i < 50; i++) sim.step(0.016f);
    EXPECT_GT(sim.time, 0.0f);
    EXPECT_GT(sim.step_count, 0u);

    // Rebuild — state should reset, not accumulate signals
    sim.build(bp);
    EXPECT_EQ(sim.state.across.size(), signals_first);
    EXPECT_EQ(sim.time, 0.0f);
    EXPECT_EQ(sim.step_count, 0u);
}

// ─── Voltage convergence ───

TEST(SimulationTest, BatteryVoltageConverges) {
    Blueprint bp = create_simple_circuit();
    SimulationController sim;
    sim.build(bp);

    // Run enough steps for SOR convergence
    for (int i = 0; i < 200; i++) sim.step(0.016f);

    float v_bat = sim.get_wire_voltage("bat.v_out");
    // Battery default v_nominal=28V, with resistor load should converge to ~26-28V
    EXPECT_GT(v_bat, 5.0f) << "Battery should produce significant voltage";
    EXPECT_LT(v_bat, 50.0f) << "Voltage should be reasonable";
}

TEST(SimulationTest, GroundRemainsZero) {
    Blueprint bp = create_simple_circuit();
    SimulationController sim;
    sim.build(bp);

    for (int i = 0; i < 200; i++) sim.step(0.016f);

    float v_gnd = sim.get_wire_voltage("gnd.v");
    EXPECT_NEAR(v_gnd, 0.0f, 0.01f) << "Ground reference should stay at 0V";
}

// ─── Port value accessor ───

TEST(SimulationTest, GetPortValue) {
    Blueprint bp = create_simple_circuit();
    SimulationController sim;
    sim.build(bp);

    for (int i = 0; i < 200; i++) sim.step(0.016f);

    // get_port_value(node_id, port_name) should equal get_wire_voltage("node.port")
    float v1 = sim.get_port_value("bat", "v_out");
    float v2 = sim.get_wire_voltage("bat.v_out");
    EXPECT_FLOAT_EQ(v1, v2);
}

TEST(SimulationTest, GetPortValue_UnknownReturnsZero) {
    Blueprint bp = create_simple_circuit();
    SimulationController sim;
    sim.build(bp);

    EXPECT_EQ(sim.get_port_value("nonexistent", "port"), 0.0f);
}

// ─── Wire energized detection ───

TEST(SimulationTest, WireIsEnergized_ActiveCircuit) {
    Blueprint bp = create_simple_circuit();
    SimulationController sim;
    sim.build(bp);

    for (int i = 0; i < 200; i++) sim.step(0.016f);

    // Battery output wire should be energized
    EXPECT_TRUE(sim.wire_is_energized("bat.v_out", 0.5f));
    // Ground wire should NOT be energized (0V)
    EXPECT_FALSE(sim.wire_is_energized("gnd.v", 0.5f));
}

TEST(SimulationTest, WireIsEnergized_NoSimulation) {
    SimulationController sim;
    // Not built yet
    EXPECT_FALSE(sim.wire_is_energized("bat.v_out"));
}

// ─── Step without build should not crash ───

TEST(SimulationTest, StepWithoutBuild_NoCrash) {
    SimulationController sim;
    sim.step(0.016f); // should do nothing
    EXPECT_EQ(sim.step_count, 0u);
}

// ─── DMR test JSON: full 5000-step simulation via editor pipeline ───

TEST(SimulationTest, DMR_5000Step_ViaEditorPipeline) {
    // Load the real DMR test JSON as a blueprint, then run simulation
    auto bp = load_blueprint_from_file(
        "/Users/vladimir/an24_cpp/src/aircraft/vsu_dmr_test.json");
    ASSERT_TRUE(bp.has_value()) << "Should load vsu_dmr_test.json";

    SimulationController sim;
    sim.build(*bp);
    ASSERT_TRUE(sim.build_result.has_value());

    // Run 5000 steps (same as the standalone JIT test)
    for (int i = 0; i < 5000; i++) {
        sim.step(0.016f);
    }

    // Verify key signals converged
    float v_bus = sim.get_wire_voltage("main_bus.v");
    float v_gnd = sim.get_wire_voltage("gnd.v");

    EXPECT_NEAR(v_gnd, 0.0f, 0.5f) << "Ground should be near 0V";
    EXPECT_GT(v_bus, 20.0f) << "Main bus should have significant voltage after 5000 steps";
    EXPECT_LT(v_bus, 40.0f) << "Bus voltage should be reasonable (28V nominal)";

    // Battery wire should be energized, ground should not
    EXPECT_TRUE(sim.wire_is_energized("bat_main_1.v_out", 0.5f));
    EXPECT_FALSE(sim.wire_is_energized("gnd.v", 0.5f));
}

// ============================================================================
// Simulator API - TDD failing tests first (RED phase)
// ============================================================================

// [simulator-001] Simulator should start empty (no components built)
TEST(SimulatorTest, StartsEmpty) {
    an24::Simulator<an24::JIT_Solver> sim;
    EXPECT_FALSE(sim.is_running());
    EXPECT_FALSE(sim.is_built());
}

// [simulator-002] start() should build components from blueprint
TEST(SimulatorTest, StartBuildsComponents) {
    Blueprint bp = create_simple_circuit();
    an24::Simulator<an24::JIT_Solver> sim;

    sim.start(bp);

    EXPECT_TRUE(sim.is_running());
    EXPECT_TRUE(sim.is_built());
}

// [simulator-003] stop() should destroy components
TEST(SimulatorTest, StopDestroysComponents) {
    Blueprint bp = create_simple_circuit();
    an24::Simulator<an24::JIT_Solver> sim;

    sim.start(bp);
    EXPECT_TRUE(sim.is_built());

    sim.stop();
    EXPECT_FALSE(sim.is_running());
    EXPECT_FALSE(sim.is_built());  // Components destroyed
}

// [simulator-004] Multiple start/stop cycles should work
TEST(SimulatorTest, MultipleStartStopCycles) {
    Blueprint bp = create_simple_circuit();
    an24::Simulator<an24::JIT_Solver> sim;

    // First cycle
    sim.start(bp);
    EXPECT_TRUE(sim.is_built());
    sim.stop();
    EXPECT_FALSE(sim.is_built());

    // Second cycle
    sim.start(bp);
    EXPECT_TRUE(sim.is_built());
    sim.stop();
    EXPECT_FALSE(sim.is_built());

    // Third cycle
    sim.start(bp);
    EXPECT_TRUE(sim.is_built());
}

// [simulator-005] After stop, get_voltage returns 0 (no component state)
TEST(SimulatorTest, AfterStopGetVoltageReturnsZero) {
    Blueprint bp = create_simple_circuit();
    an24::Simulator<an24::JIT_Solver> sim;

    sim.start(bp);

    // Run simulation to get non-zero voltage
    for (int i = 0; i < 50; i++) sim.step(0.016f);
    float v_running = sim.get_port_value("bat", "v_out");
    EXPECT_GT(v_running, 0.0f);

    // Stop simulation
    sim.stop();

    // Voltage should be 0 (components destroyed)
    float v_stopped = sim.get_port_value("bat", "v_out");
    EXPECT_FLOAT_EQ(v_stopped, 0.0f);
}

// [simulator-006] step() should do nothing if not running
TEST(SimulatorTest, StepDoesNothingIfNotRunning) {
    Blueprint bp = create_simple_circuit();
    an24::Simulator<an24::JIT_Solver> sim;

    // Don't start simulation
    EXPECT_FALSE(sim.is_running());

    // Step should be safe (no crash, no effect)
    sim.step(0.016f);

    // Time should not advance
    EXPECT_EQ(sim.get_time(), 0.0f);
}
