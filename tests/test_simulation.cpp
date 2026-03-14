#include <gtest/gtest.h>
#include "editor/simulation.h"
#include "jit_solver/simulator.h"
#include "editor/visual/persist.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "ui/core/interned_id.h"

/// Simulation integration tests

/// Helper to create a simple test circuit: gnd → battery → resistor → gnd
static Blueprint create_simple_circuit() {
    Blueprint bp;
    bp.grid_step = 16.0f;
    auto& I = bp.interner();

    // Ground reference
    Node gnd;
    gnd.id = I.intern("gnd");
    gnd.name = "Ground";
    gnd.type_name = "RefNode";
    gnd.render_hint = "ref";
    gnd.output(I.intern("v"));
    gnd.at(80, 240);
    gnd.size_wh(40, 40);
    gnd.node_content.type = NodeContentType::Value;
    gnd.node_content.value = 0.0f;
    bp.add_node(std::move(gnd));

    // Battery
    Node batt;
    batt.id = I.intern("bat");
    batt.name = "Battery";
    batt.type_name = "Battery";
    batt.input(I.intern("v_in"));
    batt.output(I.intern("v_out"));
    batt.at(80, 80);
    batt.size_wh(120, 80);
    bp.add_node(std::move(batt));

    // Resistor
    Node res;
    res.id = I.intern("res");
    res.name = "Resistor";
    res.type_name = "Resistor";
    res.input(I.intern("v_in"));
    res.output(I.intern("v_out"));
    res.at(320, 80);
    res.size_wh(120, 80);
    bp.add_node(std::move(res));

    // Ground wire: gnd.v -> bat.v_in
    Wire w1;
    w1.start.node_id = I.intern("gnd");
    w1.start.port_name = I.intern("v");
    w1.end.node_id = I.intern("bat");
    w1.end.port_name = I.intern("v_in");
    bp.add_wire(std::move(w1));

    // Battery to resistor: bat.v_out -> res.v_in
    Wire w2;
    w2.start.node_id = I.intern("bat");
    w2.start.port_name = I.intern("v_out");
    w2.end.node_id = I.intern("res");
    w2.end.port_name = I.intern("v_in");
    bp.add_wire(std::move(w2));

    // Resistor to ground: res.v_out -> gnd.v
    Wire w3;
    w3.start.node_id = I.intern("res");
    w3.start.port_name = I.intern("v_out");
    w3.end.node_id = I.intern("gnd");
    w3.end.port_name = I.intern("v");
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

// ============================================================================
// Simulator API - TDD failing tests first (RED phase)
// ============================================================================

// [simulator-001] Simulator should start empty (no components built)
TEST(SimulatorTest, StartsEmpty) {
    Simulator<JIT_Solver> sim;
    EXPECT_FALSE(sim.is_running());
    EXPECT_FALSE(sim.is_built());
}

// [simulator-002] start() should build components from blueprint
TEST(SimulatorTest, StartBuildsComponents) {
    Blueprint bp = create_simple_circuit();
    Simulator<JIT_Solver> sim;

    sim.start(bp);

    EXPECT_TRUE(sim.is_running());
    EXPECT_TRUE(sim.is_built());
}

// [simulator-003] stop() should destroy components
TEST(SimulatorTest, StopDestroysComponents) {
    Blueprint bp = create_simple_circuit();
    Simulator<JIT_Solver> sim;

    sim.start(bp);
    EXPECT_TRUE(sim.is_built());

    sim.stop();
    EXPECT_FALSE(sim.is_running());
    EXPECT_FALSE(sim.is_built());  // Components destroyed
}

// [simulator-004] Multiple start/stop cycles should work
TEST(SimulatorTest, MultipleStartStopCycles) {
    Blueprint bp = create_simple_circuit();
    Simulator<JIT_Solver> sim;

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
    Simulator<JIT_Solver> sim;

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
    Simulator<JIT_Solver> sim;

    // Don't start simulation
    EXPECT_FALSE(sim.is_running());

    // Step should be safe (no crash, no effect)
    sim.step(0.016f);

    // Time should not advance
    EXPECT_EQ(sim.get_time(), 0.0f);
}

// =============================================================================
// Merger component: 2-to-1 signal merger (inverse of Splitter)
// =============================================================================

/// Helper: circuit with Merger joining two sources into one load
static Blueprint create_merger_circuit() {
    Blueprint bp;
    bp.grid_step = 16.0f;
    auto& I = bp.interner();

    // Ground
    Node gnd;
    gnd.id = I.intern("gnd"); gnd.type_name = "RefNode"; gnd.render_hint = "ref";
    gnd.output(I.intern("v")); gnd.at(0, 0);
    bp.add_node(std::move(gnd));

    // Battery
    Node bat;
    bat.id = I.intern("bat"); bat.type_name = "Battery";
    bat.input(I.intern("v_in")); bat.output(I.intern("v_out")); bat.at(100, 0);
    bp.add_node(std::move(bat));

    // Splitter: battery → 2 branches
    Node spl;
    spl.id = I.intern("spl"); spl.type_name = "Splitter";
    spl.input(I.intern("i")); spl.output(I.intern("o1")); spl.output(I.intern("o2")); spl.at(250, 0);
    bp.add_node(std::move(spl));

    // Merger: 2 inputs → 1 output
    Node mrg;
    mrg.id = I.intern("mrg"); mrg.type_name = "Merger";
    mrg.input(I.intern("i1")); mrg.input(I.intern("i2")); mrg.output(I.intern("o")); mrg.at(400, 0);
    bp.add_node(std::move(mrg));

    // Load
    Node res;
    res.id = I.intern("res"); res.type_name = "Resistor";
    res.input(I.intern("v_in")); res.output(I.intern("v_out")); res.at(550, 0);
    bp.add_node(std::move(res));

    // Wires: gnd → bat.v_in, bat.v_out → spl.i
    Wire w1; w1.start = WireEnd(I.intern("gnd"), I.intern("v"), PortSide::Output); w1.end = WireEnd(I.intern("bat"), I.intern("v_in"), PortSide::Input);
    Wire w2; w2.start = WireEnd(I.intern("bat"), I.intern("v_out"), PortSide::Output); w2.end = WireEnd(I.intern("spl"), I.intern("i"), PortSide::Input);
    Wire w3; w3.start = WireEnd(I.intern("spl"), I.intern("o1"), PortSide::Output); w3.end = WireEnd(I.intern("mrg"), I.intern("i1"), PortSide::Input);
    Wire w4; w4.start = WireEnd(I.intern("spl"), I.intern("o2"), PortSide::Output); w4.end = WireEnd(I.intern("mrg"), I.intern("i2"), PortSide::Input);
    Wire w5; w5.start = WireEnd(I.intern("mrg"), I.intern("o"), PortSide::Output); w5.end = WireEnd(I.intern("res"), I.intern("v_in"), PortSide::Input);
    Wire w6; w6.start = WireEnd(I.intern("res"), I.intern("v_out"), PortSide::Output); w6.end = WireEnd(I.intern("gnd"), I.intern("v"), PortSide::Input);
    bp.add_wire(std::move(w1));
    bp.add_wire(std::move(w2));
    bp.add_wire(std::move(w3));
    bp.add_wire(std::move(w4));
    bp.add_wire(std::move(w5));
    bp.add_wire(std::move(w6));

    return bp;
}

TEST(SimulationTest, Merger_CircuitConverges) {
    Blueprint bp = create_merger_circuit();
    SimulationController sim;
    sim.build(bp);

    for (int i = 0; i < 200; i++) sim.step(0.016f);

    float v_bat = sim.get_wire_voltage("bat.v_out");
    float v_gnd = sim.get_wire_voltage("gnd.v");

    EXPECT_GT(v_bat, 5.0f) << "Battery output should converge to positive voltage";
    EXPECT_LT(v_bat, 50.0f) << "Battery output should be reasonable";
    EXPECT_NEAR(v_gnd, 0.0f, 0.01f) << "Ground should stay at 0V";
    EXPECT_FALSE(std::isnan(v_bat)) << "No NaN in merger circuit";
}

TEST(SimulationTest, Merger_AllPortsSameSignal) {
    // Merger aliases i1, i2 to o — all should be the same voltage
    Blueprint bp = create_merger_circuit();
    SimulationController sim;
    sim.build(bp);

    for (int i = 0; i < 200; i++) sim.step(0.016f);

    float v_i1 = sim.get_wire_voltage("mrg.i1");
    float v_i2 = sim.get_wire_voltage("mrg.i2");
    float v_o = sim.get_wire_voltage("mrg.o");

    EXPECT_FLOAT_EQ(v_i1, v_o) << "Merger i1 and o must be same signal";
    EXPECT_FLOAT_EQ(v_i2, v_o) << "Merger i2 and o must be same signal";
}

// =============================================================================
// NaN regression: floating chain endpoint diverges with SOR omega=1.8
// =============================================================================

TEST(SimulationTest, NaN_Regression_FloatingChainDoesNotExplode) {
    // Reproduces the user's bug: battery → lamps → dangling (no ground on output)
    // This circuit has a floating endpoint but should NOT produce NaN
    // (parasitic conductance and clamping should prevent it)
    Blueprint bp;
    bp.grid_step = 16.0f;

    auto& I = bp.interner();

    Node gnd; gnd.id = I.intern("gnd"); gnd.type_name = "RefNode"; gnd.render_hint = "ref";
    gnd.output(I.intern("v")); gnd.at(0, 0);
    bp.add_node(std::move(gnd));

    Node bat; bat.id = I.intern("bat"); bat.type_name = "Battery";
    bat.input(I.intern("v_in")); bat.output(I.intern("v_out")); bat.at(100, 0);
    bp.add_node(std::move(bat));

    Node lamp; lamp.id = I.intern("lamp"); lamp.type_name = "IndicatorLight";
    lamp.input(I.intern("v_in")); lamp.output(I.intern("v_out")); lamp.output(I.intern("brightness")); lamp.at(300, 0);
    bp.add_node(std::move(lamp));

    // gnd → bat.v_in, bat.v_out → lamp.v_in, lamp.v_out → DANGLING
    Wire w1; w1.start = WireEnd(I.intern("gnd"), I.intern("v"), PortSide::Output); w1.end = WireEnd(I.intern("bat"), I.intern("v_in"), PortSide::Input);
    Wire w2; w2.start = WireEnd(I.intern("bat"), I.intern("v_out"), PortSide::Output); w2.end = WireEnd(I.intern("lamp"), I.intern("v_in"), PortSide::Input);
    bp.add_wire(std::move(w1));
    bp.add_wire(std::move(w2));
    // NO wire from lamp.v_out to ground — intentionally floating

    SimulationController sim;
    sim.build(bp);

    // Run for 2 simulated seconds (120 steps at 60Hz)
    for (int i = 0; i < 120; i++) {
        sim.step(0.016f);
        float v = sim.get_wire_voltage("bat.v_out");
        ASSERT_FALSE(std::isnan(v)) << "NaN at step " << i << " — floating chain diverged";
        ASSERT_FALSE(std::isinf(v)) << "Inf at step " << i << " — floating chain diverged";
    }
}

TEST(SimulationTest, TwoRefNodes_CircuitStable) {
    // Two separate RefNodes (one for battery, one for load) with a complete loop
    Blueprint bp;
    bp.grid_step = 16.0f;

    auto& I = bp.interner();

    Node gnd1; gnd1.id = I.intern("gnd1"); gnd1.type_name = "RefNode"; gnd1.render_hint = "ref";
    gnd1.output(I.intern("v")); gnd1.at(0, 0);
    bp.add_node(std::move(gnd1));

    Node gnd2; gnd2.id = I.intern("gnd2"); gnd2.type_name = "RefNode"; gnd2.render_hint = "ref";
    gnd2.output(I.intern("v")); gnd2.at(600, 0);
    bp.add_node(std::move(gnd2));

    Node bat; bat.id = I.intern("bat"); bat.type_name = "Battery";
    bat.input(I.intern("v_in")); bat.output(I.intern("v_out")); bat.at(100, 0);
    bp.add_node(std::move(bat));

    Node res; res.id = I.intern("res"); res.type_name = "Resistor";
    res.input(I.intern("v_in")); res.output(I.intern("v_out")); res.at(300, 0);
    bp.add_node(std::move(res));

    // gnd1 → bat.v_in, bat.v_out → res.v_in, res.v_out → gnd2
    Wire w1; w1.start = WireEnd(I.intern("gnd1"), I.intern("v"), PortSide::Output); w1.end = WireEnd(I.intern("bat"), I.intern("v_in"), PortSide::Input);
    Wire w2; w2.start = WireEnd(I.intern("bat"), I.intern("v_out"), PortSide::Output); w2.end = WireEnd(I.intern("res"), I.intern("v_in"), PortSide::Input);
    Wire w3; w3.start = WireEnd(I.intern("res"), I.intern("v_out"), PortSide::Output); w3.end = WireEnd(I.intern("gnd2"), I.intern("v"), PortSide::Input);
    bp.add_wire(std::move(w1));
    bp.add_wire(std::move(w2));
    bp.add_wire(std::move(w3));

    SimulationController sim;
    sim.build(bp);

    for (int i = 0; i < 200; i++) {
        sim.step(0.016f);
        float v = sim.get_wire_voltage("bat.v_out");
        ASSERT_FALSE(std::isnan(v)) << "NaN at step " << i;
        ASSERT_FALSE(std::isinf(v)) << "Inf at step " << i;
    }

    float v_bat = sim.get_wire_voltage("bat.v_out");
    EXPECT_GT(v_bat, 5.0f) << "Battery should produce voltage with two separate RefNodes";
    EXPECT_LT(v_bat, 50.0f) << "Voltage should be reasonable";
}
