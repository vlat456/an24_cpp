#include <gtest/gtest.h>
#include "jit_solver/simulator.h"
#include <cmath>

TEST(ProductionPathPushRuntime, SinglePassSettlesLinearChain) {
    const std::string json = R"({
        "devices": [
            {"name": "mul", "classname": "Multiply"},
            {"name": "add", "classname": "Add"},
            {"name": "clamp", "classname": "Clamp"},
            {"name": "ra", "classname": "RefNode", "params": {"value": "2.0"}},
            {"name": "rb", "classname": "RefNode", "params": {"value": "4.0"}},
            {"name": "rc", "classname": "RefNode", "params": {"value": "3.0"}},
            {"name": "v_min", "classname": "Value", "params": {"value": "0.0"}},
            {"name": "v_max", "classname": "Value", "params": {"value": "20.0"}}
        ],
        "connections": [
            {"from": "ra.v", "to": "add.A"},
            {"from": "rb.v", "to": "add.B"},
            {"from": "add.o", "to": "mul.A"},
            {"from": "rc.v", "to": "mul.B"},
            {"from": "mul.o", "to": "clamp.in"},
            {"from": "v_min.o", "to": "clamp.min"},
            {"from": "v_max.o", "to": "clamp.max"}
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start_from_json(json));
    sim.step(1.0f / 60.0f);

    EXPECT_NEAR(sim.get_port_value("clamp", "out"), 18.0f, 1e-4f);
}

TEST(ProductionPathPushRuntime, CycleRemainsFinite) {
    const std::string json = R"({
        "devices": [
            {"name": "add1", "classname": "Add"},
            {"name": "add2", "classname": "Add"},
            {"name": "ref1", "classname": "RefNode", "params": {"value": "1.0"}},
            {"name": "ref2", "classname": "RefNode", "params": {"value": "2.0"}}
        ],
        "connections": [
            {"from": "add2.o", "to": "add1.A"},
            {"from": "ref1.v", "to": "add1.B"},
            {"from": "add1.o", "to": "add2.A"},
            {"from": "ref2.v", "to": "add2.B"}
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start_from_json(json));
    for (int i = 0; i < 10; ++i) {
        sim.step(1.0f / 60.0f);
    }

    EXPECT_TRUE(std::isfinite(sim.get_port_value("add1", "o")));
    EXPECT_TRUE(std::isfinite(sim.get_port_value("add2", "o")));
}
