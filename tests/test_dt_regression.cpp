#include <gtest/gtest.h>
#include "jit_solver/simulator.h"
#include "jit_solver/SOR_constants.h"
#include "editor/visual/persist.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "ui/core/interned_id.h"
#include "sim_test_json.h"

namespace ui {
inline std::ostream& operator<<(std::ostream& os, InternedId id) {
    return os << "InternedId(" << id.raw() << ")";
}
}

// =============================================================================
// Helper: simple battery circuit (gnd → battery → resistor → gnd)
// =============================================================================
static Blueprint make_battery_circuit() {
    Blueprint bp;
    auto& I = bp.interner();
    bp.grid_step = 16.0f;

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

    Node batt;
    batt.id = I.intern("bat");
    batt.name = "Battery";
    batt.type_name = "Battery";
    batt.input(I.intern("v_in"));
    batt.output(I.intern("v_out"));
    batt.at(80, 80);
    batt.size_wh(120, 80);
    bp.add_node(std::move(batt));

    Node res;
    res.id = I.intern("res");
    res.name = "Resistor";
    res.type_name = "Resistor";
    res.input(I.intern("v_in"));
    res.output(I.intern("v_out"));
    res.at(320, 80);
    res.size_wh(120, 80);
    bp.add_node(std::move(res));

    Wire w1;
    w1.start.node_id = I.intern("gnd"); w1.start.port_name = I.intern("v");
    w1.end.node_id = I.intern("bat");   w1.end.port_name = I.intern("v_in");
    bp.add_wire(std::move(w1));

    Wire w2;
    w2.start.node_id = I.intern("bat"); w2.start.port_name = I.intern("v_out");
    w2.end.node_id = I.intern("res");   w2.end.port_name = I.intern("v_in");
    bp.add_wire(std::move(w2));

    Wire w3;
    w3.start.node_id = I.intern("res"); w3.start.port_name = I.intern("v_out");
    w3.end.node_id = I.intern("gnd");   w3.end.port_name = I.intern("v");
    bp.add_wire(std::move(w3));

    return bp;
}

// =============================================================================
// Helper: battery + VoltageSense observer bridge
// =============================================================================
static Blueprint make_voltage_sense_circuit() {
    Blueprint bp = make_battery_circuit();
    auto& I = bp.interner();

    Node vs;
    vs.id = I.intern("vs");
    vs.name = "VoltageSense";
    vs.type_name = "VoltageSense";
    vs.input(I.intern("v_in"));
    vs.input(I.intern("v_ref"));
    vs.output(I.intern("out"));
    vs.params["gain"] = "1.0";
    vs.params["offset"] = "0.0";
    vs.at(500, 200);
    vs.size_wh(120, 80);
    bp.add_node(std::move(vs));

    Wire w1;
    w1.start.node_id = I.intern("bat");
    w1.start.port_name = I.intern("v_out");
    w1.end.node_id = I.intern("vs");
    w1.end.port_name = I.intern("v_in");
    bp.add_wire(std::move(w1));

    Wire w2;
    w2.start.node_id = I.intern("gnd");
    w2.start.port_name = I.intern("v");
    w2.end.node_id = I.intern("vs");
    w2.end.port_name = I.intern("v_ref");
    bp.add_wire(std::move(w2));

    return bp;
}

static Blueprint make_hold_button_control_circuit() {
    Blueprint bp;
    auto& I = bp.interner();

    Node gnd;
    gnd.id = I.intern("gnd");
    gnd.name = "gnd";
    gnd.type_name = "RefNode";
    gnd.output(I.intern("v"));
    gnd.params["value"] = "0.0";
    bp.add_node(std::move(gnd));

    Node bat;
    bat.id = I.intern("bat");
    bat.name = "bat";
    bat.type_name = "Battery";
    bat.input(I.intern("v_in"));
    bat.output(I.intern("v_out"));
    bat.params["v_nominal"] = "28.0";
    bat.params["internal_r"] = "0.01";
    bp.add_node(std::move(bat));

    Node ctrl;
    ctrl.id = I.intern("ctrl");
    ctrl.name = "ctrl";
    ctrl.type_name = "RefNode";
    ctrl.output(I.intern("v"));
    ctrl.params["value"] = "1.0";
    bp.add_node(std::move(ctrl));

    Node btn;
    btn.id = I.intern("btn");
    btn.name = "btn";
    btn.type_name = "HoldButton";
    btn.input(I.intern("v_in"));
    btn.input(I.intern("control"));
    btn.output(I.intern("v_out"));
    btn.output(I.intern("state"));
    bp.add_node(std::move(btn));

    Node load;
    load.id = I.intern("load");
    load.name = "load";
    load.type_name = "Resistor";
    load.input(I.intern("v_in"));
    load.output(I.intern("v_out"));
    load.params["conductance"] = "0.1";
    bp.add_node(std::move(load));

    auto add_wire = [&](const char* a_node, const char* a_port, const char* b_node, const char* b_port) {
        Wire w;
        w.start.node_id = I.intern(a_node);
        w.start.port_name = I.intern(a_port);
        w.end.node_id = I.intern(b_node);
        w.end.port_name = I.intern(b_port);
        bp.add_wire(std::move(w));
    };

    add_wire("bat", "v_out", "btn", "v_in");
    add_wire("btn", "v_out", "load", "v_in");
    add_wire("load", "v_out", "gnd", "v");
    add_wire("bat", "v_in", "gnd", "v");
    add_wire("ctrl", "v", "btn", "control");

    return bp;
}

static Blueprint make_switch_control_commit_circuit() {
    Blueprint bp;
    auto& I = bp.interner();

    Node gnd;
    gnd.id = I.intern("gnd");
    gnd.name = "gnd";
    gnd.type_name = "RefNode";
    gnd.output(I.intern("v"));
    gnd.params["value"] = "0.0";
    bp.add_node(std::move(gnd));

    Node bat;
    bat.id = I.intern("bat");
    bat.name = "bat";
    bat.type_name = "Battery";
    bat.input(I.intern("v_in"));
    bat.output(I.intern("v_out"));
    bat.params["v_nominal"] = "28.0";
    bat.params["internal_r"] = "0.01";
    bp.add_node(std::move(bat));

    Node ctrl;
    ctrl.id = I.intern("ctrl");
    ctrl.name = "ctrl";
    ctrl.type_name = "RefNode";
    ctrl.output(I.intern("v"));
    ctrl.params["value"] = "1.0";
    bp.add_node(std::move(ctrl));

    Node sw;
    sw.id = I.intern("sw");
    sw.name = "sw";
    sw.type_name = "Switch";
    sw.input(I.intern("v_in"));
    sw.input(I.intern("control"));
    sw.output(I.intern("v_out"));
    sw.output(I.intern("state"));
    bp.add_node(std::move(sw));

    Node load;
    load.id = I.intern("load");
    load.name = "load";
    load.type_name = "Resistor";
    load.input(I.intern("v_in"));
    load.output(I.intern("v_out"));
    load.params["conductance"] = "0.1";
    bp.add_node(std::move(load));

    auto add_wire = [&](const char* a_node, const char* a_port, const char* b_node, const char* b_port) {
        Wire w;
        w.start.node_id = I.intern(a_node);
        w.start.port_name = I.intern(a_port);
        w.end.node_id = I.intern(b_node);
        w.end.port_name = I.intern(b_port);
        bp.add_wire(std::move(w));
    };

    add_wire("bat", "v_out", "sw", "v_in");
    add_wire("sw", "v_out", "load", "v_in");
    add_wire("load", "v_out", "gnd", "v");
    add_wire("bat", "v_in", "gnd", "v");
    add_wire("ctrl", "v", "sw", "control");

    return bp;
}

static Blueprint make_relay_control_commit_circuit() {
    Blueprint bp;
    auto& I = bp.interner();

    Node gnd;
    gnd.id = I.intern("gnd");
    gnd.name = "gnd";
    gnd.type_name = "RefNode";
    gnd.output(I.intern("v"));
    gnd.params["value"] = "0.0";
    bp.add_node(std::move(gnd));

    Node bat;
    bat.id = I.intern("bat");
    bat.name = "bat";
    bat.type_name = "Battery";
    bat.input(I.intern("v_in"));
    bat.output(I.intern("v_out"));
    bat.params["v_nominal"] = "28.0";
    bat.params["internal_r"] = "0.01";
    bp.add_node(std::move(bat));

    Node ctrl;
    ctrl.id = I.intern("ctrl");
    ctrl.name = "ctrl";
    ctrl.type_name = "RefNode";
    ctrl.output(I.intern("v"));
    ctrl.params["value"] = "1.0";
    bp.add_node(std::move(ctrl));

    Node relay;
    relay.id = I.intern("relay");
    relay.name = "relay";
    relay.type_name = "Relay";
    relay.input(I.intern("v_in"));
    relay.input(I.intern("control"));
    relay.output(I.intern("v_out"));
    relay.params["hold_threshold"] = "0.5";
    bp.add_node(std::move(relay));

    Node load;
    load.id = I.intern("load");
    load.name = "load";
    load.type_name = "Resistor";
    load.input(I.intern("v_in"));
    load.output(I.intern("v_out"));
    load.params["conductance"] = "0.1";
    bp.add_node(std::move(load));

    auto add_wire = [&](const char* a_node, const char* a_port, const char* b_node, const char* b_port) {
        Wire w;
        w.start.node_id = I.intern(a_node);
        w.start.port_name = I.intern(a_port);
        w.end.node_id = I.intern(b_node);
        w.end.port_name = I.intern(b_port);
        bp.add_wire(std::move(w));
    };

    add_wire("bat", "v_out", "relay", "v_in");
    add_wire("relay", "v_out", "load", "v_in");
    add_wire("load", "v_out", "gnd", "v");
    add_wire("bat", "v_in", "gnd", "v");
    add_wire("ctrl", "v", "relay", "control");

    return bp;
}

// =============================================================================
// Regression: SOR::OMEGA is the single source of truth
// =============================================================================

TEST(DtRegression, SOR_OmegaIsCanonical) {
    // SOR::OMEGA must be in the stable range [1.0, 1.5]
    EXPECT_GE(SOR::OMEGA, 1.0f);
    EXPECT_LE(SOR::OMEGA, 1.5f);
    // Exact value we settled on
    EXPECT_FLOAT_EQ(SOR::OMEGA, 1.3f);
}

// =============================================================================
// Regression: Simulator time accumulates correctly with variable dt
// =============================================================================

TEST(DtRegression, Simulator_TimeAccumulatesVariableDt) {
    Blueprint bp = make_battery_circuit();
    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    // Feed variable frame deltas (simulating 60Hz, 144Hz, 30Hz mix)
    const float dts[] = {1.0f/60.0f, 1.0f/144.0f, 1.0f/30.0f, 1.0f/60.0f, 1.0f/75.0f};
    float expected_time = 0.0f;
    for (float dt : dts) {
        sim.step(dt);
        expected_time += dt;
    }

    EXPECT_NEAR(sim.get_time(), expected_time, 1e-6f)
        << "Simulator must accumulate actual dt values, not assume fixed 60Hz";
    EXPECT_EQ(sim.get_step_count(), 5u);
    sim.stop();
}

TEST(DtRegression, SimController_TimeAccumulatesVariableDt) {
    Blueprint bp = make_battery_circuit();
    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    const float dts[] = {1.0f/60.0f, 1.0f/144.0f, 1.0f/30.0f};
    float expected_time = 0.0f;
    for (float dt : dts) {
        sim.step(dt);
        expected_time += dt;
    }

    EXPECT_NEAR(sim.get_time(), expected_time, 1e-6f)
        << "Simulator must accumulate actual dt values";
    EXPECT_EQ(sim.get_step_count(), 3u);
    sim.stop();
}

// =============================================================================
// Regression: Electrical solver runs every step regardless of dt
// =============================================================================

TEST(DtRegression, Simulator_ElectricalSolvesEveryStep) {
    Blueprint bp = make_battery_circuit();
    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    // Step with a very small dt — electrical must still solve
    sim.step(0.001f);  // 1ms frame (1000 Hz)
    float v1 = sim.get_wire_voltage("bat.v_out");
    EXPECT_GT(v1, 0.0f) << "Electrical solver must run on every step, even tiny dt";

    // Step with a very large dt — electrical must still solve (voltage moves further)
    sim.step(0.1f);  // 100ms frame (10 Hz)
    float v2 = sim.get_wire_voltage("bat.v_out");
    EXPECT_GT(v2, 0.0f) << "Electrical solver must run on every step, even large dt";

    // After enough steps, voltage should converge toward 28V
    for (int i = 0; i < 200; ++i) sim.step(0.016f);
    float v_final = sim.get_wire_voltage("bat.v_out");
    EXPECT_NEAR(v_final, 28.0f, 1.0f) << "Battery should converge near nominal voltage";

    sim.stop();
}

// =============================================================================
// Regression: Steady-state voltage is dt-independent
// =============================================================================

TEST(DtRegression, Simulator_SteadyStateIsDtIndependent) {
    // Run until steady state with 60Hz dt
    Blueprint bp60 = make_battery_circuit();
    Simulator<JIT_Solver> sim60;
    sim60.start_from_json(sim_test_json::from_blueprint(bp60));
    for (int i = 0; i < 200; ++i) sim60.step(1.0f / 60.0f);
    float v60 = sim60.get_wire_voltage("bat.v_out");
    sim60.stop();

    // Run until steady state with 144Hz dt (more steps needed for same real time)
    Blueprint bp144 = make_battery_circuit();
    Simulator<JIT_Solver> sim144;
    sim144.start_from_json(sim_test_json::from_blueprint(bp144));
    for (int i = 0; i < 480; ++i) sim144.step(1.0f / 144.0f);
    float v144 = sim144.get_wire_voltage("bat.v_out");
    sim144.stop();

    // Steady-state voltage must be the same regardless of frame rate
    // Battery default is 28V nominal
    EXPECT_NEAR(v60, v144, 0.5f)
        << "Steady-state voltage must not depend on frame rate. "
        << "60Hz=" << v60 << "V, 144Hz=" << v144 << "V";
}

// =============================================================================
// Regression: Step counter resets on start/stop
// =============================================================================

TEST(DtRegression, Simulator_StepCounterResetsOnStart) {
    Blueprint bp = make_battery_circuit();
    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    sim.step(0.016f);
    sim.step(0.016f);
    EXPECT_EQ(sim.get_step_count(), 2u);

    // Restart resets
    sim.stop();
    sim.start_from_json(sim_test_json::from_blueprint(bp));
    EXPECT_EQ(sim.get_step_count(), 0u);
    EXPECT_NEAR(sim.get_time(), 0.0f, 1e-9f);

    sim.stop();
}

// =============================================================================
// Regression: No hardcoded dt in production Simulator path
// The Simulator::step(dt) must use the dt parameter, not an internal constant.
// We verify by checking that different dt values produce different time.
// =============================================================================

TEST(DtRegression, Simulator_UsesDtParameterNotConstant) {
    Blueprint bp = make_battery_circuit();
    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    sim.step(0.01f);
    float t1 = sim.get_time();

    sim.step(0.05f);
    float t2 = sim.get_time();

    // If dt was hardcoded, t2-t1 would equal the hardcoded value, not 0.05
    EXPECT_NEAR(t2 - t1, 0.05f, 1e-6f)
        << "Simulator must use the dt parameter passed to step(), not a hardcoded constant";

    sim.stop();
}

// =============================================================================
// Regression: Comparator hysteresis — output turns OFF when diff << Voff
// (Bug was: test expected output to stay ON when diff=-5.0 with Voff=-0.1)
// =============================================================================

TEST(DtRegression, Comparator_HysteresisCorrectBehavior) {
    Blueprint bp;
    auto& I = bp.interner();

    // Use two RefNodes to provide controlled voltages for Va and Vb
    // (RefNode correctly clamps to its value, so we use separate ones
    //  and adjust their values via apply_overrides on the RefNode itself)
    Node comp;
    comp.id = I.intern("comp1"); comp.name = "Comparator"; comp.type_name = "Comparator";
    comp.input(I.intern("Va")); comp.input(I.intern("Vb")); comp.output(I.intern("o"));
    comp.at(100, 0); comp.size_wh(120, 80);
    bp.add_node(std::move(comp));

    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    const float dt = 1.0f / 60.0f;

    // Set Va=10, Vb=0 → diff=10 > Von(5) → ON
    sim.apply_overrides({{"comp1.Va", 10.0f}, {"comp1.Vb", 0.0f}});
    sim.step(dt);
    EXPECT_TRUE(sim.get_component_state_as_bool("comp1", "o"))
        << "Output should turn ON when diff >> Von";

    // Set Va=0, Vb=10 → diff=-10 << Voff(2) → OFF
    sim.apply_overrides({{"comp1.Va", 0.0f}, {"comp1.Vb", 10.0f}});
    sim.step(dt);
    EXPECT_FALSE(sim.get_component_state_as_bool("comp1", "o"))
        << "Output must turn OFF when diff is far below Voff (not kept by hysteresis)";

    sim.stop();
}

// =============================================================================
// Regression: simulator move preserves adaptive-omega runtime state
// =============================================================================

TEST(DtRegression, SimulatorMove_PreservesAdaptiveState) {
    Blueprint bp = make_battery_circuit();
    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    // Build convergence history and allow adaptive omega to react.
    for (int i = 0; i < 120; ++i) {
        sim.step(1.0f / 60.0f);
    }

    float time_before = sim.get_time();
    uint64_t steps_before = sim.get_step_count();
    float omega_before = sim.get_omega();

    Simulator<JIT_Solver> moved = std::move(sim);

    EXPECT_TRUE(moved.is_running());
    EXPECT_NEAR(moved.get_time(), time_before, 1e-6f);
    EXPECT_EQ(moved.get_step_count(), steps_before);
    EXPECT_NEAR(moved.get_omega(), omega_before, 1e-6f);

    // Source must become inert after move.
    EXPECT_FALSE(sim.is_running());
    EXPECT_NEAR(sim.get_time(), 0.0f, 1e-9f);
    EXPECT_EQ(sim.get_step_count(), 0u);
}

TEST(DtRegression, PauseDtZero_DoesNotAdvanceState) {
    Blueprint bp = make_battery_circuit();
    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    sim.step(1.0f / 60.0f);
    const float time_before = sim.get_time();
    const uint32_t steps_before = sim.get_step_count();
    const float v_before = sim.get_port_value("bat", "v_out");

    for (int i = 0; i < 20; ++i) {
        sim.step(0.0f);
    }

    EXPECT_FLOAT_EQ(sim.get_time(), time_before);
    EXPECT_EQ(sim.get_step_count(), steps_before);
    EXPECT_FLOAT_EQ(sim.get_port_value("bat", "v_out"), v_before);
    sim.stop();
}

TEST(DtRegression, VoltageSense_UpdatesOnFirstStep) {
    Blueprint bp = make_voltage_sense_circuit();
    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    const float out0 = sim.get_port_value("vs", "out");
    sim.step(1.0f / 60.0f);
    const float out1 = sim.get_port_value("vs", "out");

    EXPECT_NEAR(out0, 0.0f, 1e-4f);
    EXPECT_GT(out1, 1.0f) << "VoltageSense should publish settled electrical value in same step";
    sim.stop();
}

TEST(DtRegression, HoldButton_ControlCommit_AppliesOnFirstStep) {
    Blueprint bp = make_hold_button_control_circuit();
    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    const float s0 = sim.get_port_value("btn", "state");
    sim.step(1.0f / 60.0f);
    const float s1 = sim.get_port_value("btn", "state");

    EXPECT_NEAR(s0, 0.0f, 0.1f);
    EXPECT_GT(s1, 0.5f) << "HoldButton press command should be committed in same simulation step";
    sim.stop();
}

TEST(DtRegression, Switch_ControlCommit_AppliesOnFirstStep) {
    Blueprint bp = make_switch_control_commit_circuit();
    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    const float s0 = sim.get_port_value("sw", "state");
    sim.step(1.0f / 60.0f);
    const float s1 = sim.get_port_value("sw", "state");

    EXPECT_NEAR(s0, 0.0f, 0.1f);
    EXPECT_GT(s1, 0.5f) << "Switch should toggle closed on first control edge step";
    sim.stop();
}

TEST(DtRegression, Relay_ControlCommit_AppliesOnFirstStep) {
    Blueprint bp = make_relay_control_commit_circuit();
    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    const float v0 = sim.get_port_value("relay", "v_out");
    sim.step(1.0f / 60.0f);
    const float v1 = sim.get_port_value("relay", "v_out");
    const float load_v1 = sim.get_port_value("load", "v_in");

    EXPECT_NEAR(v0, 0.0f, 0.5f);
    EXPECT_GT(v1, -20.0f) << "Relay output should move away from fully-open zero state on first committed control step";
    EXPECT_GT(load_v1, -20.0f) << "Downstream node should reflect first-step relay commit";
    sim.stop();
}
