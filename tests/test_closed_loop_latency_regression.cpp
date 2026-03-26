#include <gtest/gtest.h>

#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "jit_solver/simulator.h"
#include "sim_test_json.h"
#include "ui/core/interned_id.h"

namespace ui {
inline std::ostream& operator<<(std::ostream& os, InternedId id) {
    return os << "InternedId(" << id.raw() << ")";
}
} // namespace ui

static Blueprint make_logical_to_actuator_blueprint() {
    Blueprint bp;
    auto& I = bp.interner();
    bp.grid_step = 16.0f;

    Node gnd;
    gnd.id = I.intern("gnd");
    gnd.name = "gnd";
    gnd.type_name = "RefNode";
    gnd.output(I.intern("v"));
    gnd.params["value"] = "0.0";
    gnd.node_content.type = NodeContentType::Value;
    gnd.node_content.value = 0.0f;
    bp.add_node(std::move(gnd));

    Node cmd_input;
    cmd_input.id = I.intern("cmd_input");
    cmd_input.name = "cmd_input";
    cmd_input.type_name = "RefNode";
    cmd_input.output(I.intern("v"));
    cmd_input.params["value"] = "1.0";
    cmd_input.node_content.type = NodeContentType::Value;
    cmd_input.node_content.value = 1.0f;
    bp.add_node(std::move(cmd_input));

    Node cmd_scale;
    cmd_scale.id = I.intern("cmd_scale");
    cmd_scale.name = "cmd_scale";
    cmd_scale.type_name = "RefNode";
    cmd_scale.output(I.intern("v"));
    cmd_scale.params["value"] = "30.0";
    cmd_scale.node_content.type = NodeContentType::Value;
    cmd_scale.node_content.value = 30.0f;
    bp.add_node(std::move(cmd_scale));

    Node source;
    source.id = I.intern("cvs");
    source.name = "cvs";
    source.type_name = "ControlledVoltageSource";
    source.input(I.intern("cmd"));
    source.input(I.intern("v_neg"));
    source.output(I.intern("v_pos"));
    source.params["gain"] = "1.0";
    source.params["offset"] = "0.0";
    source.params["min_v"] = "0.0";
    source.params["max_v"] = "40.0";
    source.params["r_internal"] = "0.1";
    bp.add_node(std::move(source));

    Node gain;
    gain.id = I.intern("mul");
    gain.name = "mul";
    gain.type_name = "Multiply";
    gain.input(I.intern("A"));
    gain.input(I.intern("B"));
    gain.output(I.intern("o"));
    bp.add_node(std::move(gain));

    Node load;
    load.id = I.intern("load");
    load.name = "load";
    load.type_name = "HighPowerLoad";
    load.input(I.intern("v_in"));
    load.output(I.intern("v_out"));
    load.params["power_draw"] = "100.0";
    load.params["min_voltage_diff"] = "0.01";
    bp.add_node(std::move(load));

    Node meter;
    meter.id = I.intern("meter");
    meter.name = "meter";
    meter.type_name = "Voltmeter";
    meter.input(I.intern("v_in"));
    bp.add_node(std::move(meter));

    auto add_wire = [&](const char* a_node, const char* a_port, const char* b_node, const char* b_port) {
        Wire w;
        w.start.node_id = I.intern(a_node);
        w.start.port_name = I.intern(a_port);
        w.end.node_id = I.intern(b_node);
        w.end.port_name = I.intern(b_port);
        bp.add_wire(std::move(w));
    };

    add_wire("gnd", "v", "cvs", "v_neg");
    add_wire("gnd", "v", "load", "v_out");

    add_wire("cvs", "v_pos", "load", "v_in");
    add_wire("cvs", "v_pos", "meter", "v_in");

    add_wire("cmd_input", "v", "mul", "A");
    add_wire("cmd_scale", "v", "mul", "B");
    add_wire("mul", "o", "cvs", "cmd");

    return bp;
}

TEST(ClosedLoopLatency, LogicalCommandAffectsBusOnFirstStep) {
    Blueprint bp = make_logical_to_actuator_blueprint();

    Simulator<JIT_Solver> sim;
    sim.start_from_json(sim_test_json::from_blueprint(bp));

    const float v0 = sim.get_port_value("cvs", "v_pos");
    sim.step(1.0f / 60.0f);
    const float v1 = sim.get_port_value("cvs", "v_pos");

    EXPECT_NEAR(v0, 0.0f, 0.5f);
    EXPECT_GT(v1, 5.0f) << "Closed loop should move bus voltage on the very first step";

    sim.stop();
}
