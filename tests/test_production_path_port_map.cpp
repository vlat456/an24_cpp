#include <gtest/gtest.h>
#include "core/solvers/jit/simulator.h"

TEST(ProductionPathPortMap, AndGateReadsWiredInputs) {
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

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(build_input_from_json(json)));
    for (int i = 0; i < 20; ++i) {
        sim.step(1.0f / 60.0f);
    }

    EXPECT_GT(sim.get_port_value("bus", "v"), 20.0f);
    EXPECT_NEAR(sim.get_port_value("v2b", "o"), 1.0f, 0.01f);
    EXPECT_NEAR(sim.get_port_value("hb", "state"), 0.0f, 0.01f);
    EXPECT_NEAR(sim.get_port_value("and_1", "o"), 0.0f, 0.01f)
        << "AND(1,0) must output 0 on production path";
}

TEST(ProductionPathPortMap, NotGateReadsCorrectInput) {
    const char* json = R"({
        "devices": [
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
            {"name": "bat", "classname": "ElectricalSource", "params": {
                "voltage": "28.0", "resistance": "0.01"
            }},
            {"name": "v2b", "classname": "Positive_V_to_Bool"},
            {"name": "not_1", "classname": "NOT"}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "v2b.Vin"},
            {"from": "v2b.o", "to": "not_1.A"}
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(build_input_from_json(json)));
    for (int i = 0; i < 20; ++i) {
        sim.step(1.0f / 60.0f);
    }

    EXPECT_NEAR(sim.get_port_value("v2b", "o"), 1.0f, 0.01f);
    EXPECT_NEAR(sim.get_port_value("not_1", "o"), 0.0f, 0.01f)
        << "NOT(1) must output 0 on production path";
}

TEST(ProductionPathPortMap, SubtractReadsBothInputs) {
    const char* json = R"({
        "devices": [
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
            {"name": "bat", "classname": "ElectricalSource", "params": {
                "voltage": "28.0", "resistance": "0.01"
            }},
            {"name": "sub", "classname": "Subtract"}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "sub.A"},
            {"from": "gnd.v", "to": "sub.B"}
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(build_input_from_json(json)));
    for (int i = 0; i < 20; ++i) {
        sim.step(1.0f / 60.0f);
    }

    EXPECT_GT(sim.get_port_value("sub", "o"), 20.0f)
        << "Subtract(28, 0) must output ~28 on production path";
}
