#include <gtest/gtest.h>

#include "jit_solver/jit_solver.h"
#include "jit_solver/simulator.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"

#include <cmath>
#include <stdexcept>

namespace {

DeviceInstance make_device(const std::string& name,
                           const std::string& classname,
                           const std::unordered_map<std::string, std::string>& params = {}) {
    DeviceInstance dev;
    dev.name = name;
    dev.classname = classname;
    dev.params = params;
    dev.execution = {};

    auto ports = get_component_ports(classname);
    if (ports.empty() && classname == "MaxSelector") {
        ports = {"A", "B", "o"};
    }
    for (const auto& port_name : ports) {
        dev.ports[port_name] = Port{PortDirection::InOut, PortType::Any};
    }
    return dev;
}

SimulationState make_state(uint32_t signal_count) {
    SimulationState st;
    for (uint32_t i = 0; i < signal_count; ++i) {
        (void)st.allocate_signal(0.0f, {Domain::Electrical, true});
    }
    return st;
}

} // namespace

TEST(PushRuntime, SinglePassSettlesLinearChain) {
    std::vector<DeviceInstance> devices = {
        make_device("mul", "Multiply"),
        make_device("add", "Add"),
        make_device("clamp", "Clamp", {{"min", "0.0"}, {"max", "20.0"}}),
        make_device("ra", "RefNode", {{"value", "2.0"}}),
        make_device("rb", "RefNode", {{"value", "4.0"}}),
        make_device("rc", "RefNode", {{"value", "3.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"ra.v", "add.A"},
        {"rb.v", "add.B"},
        {"add.o", "mul.A"},
        {"rc.v", "mul.B"},
        {"mul.o", "clamp.in"}
    };

    auto result = build_systems_dev(devices, connections);
    auto st = make_state(result.signal_count);

    result.scheduler.step(st, 1.0f / 60.0f);

    const uint32_t out_sig = result.port_to_signal.at("clamp.out");
    EXPECT_NEAR(st.values[out_sig], 18.0f, 1e-4f);
}

TEST(PushRuntime, CycleUsesOneFrameDelay) {
    std::vector<DeviceInstance> devices = {
        make_device("add1", "Add"),
        make_device("add2", "Add"),
        make_device("ref1", "RefNode", {{"value", "1.0"}}),
        make_device("ref2", "RefNode", {{"value", "2.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"add2.o", "add1.A"},
        {"ref1.v", "add1.B"},
        {"add1.o", "add2.A"},
        {"ref2.v", "add2.B"}
    };

    auto result = build_systems_dev(devices, connections);
    auto st = make_state(result.signal_count);

    for (int i = 0; i < 10; ++i) {
        EXPECT_NO_THROW(result.scheduler.step(st, 1.0f / 60.0f));
    }

    EXPECT_TRUE(std::isfinite(st.values[result.port_to_signal.at("add1.o")]));
    EXPECT_TRUE(std::isfinite(st.values[result.port_to_signal.at("add2.o")]));
}

TEST(PushRuntime, DynamicEnableDisableStable) {
    const std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery", "params": {"v_nominal": "28.0"}},
            {"name": "sw", "classname": "Switch", "params": {"closed": "false"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "sw.v_in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    sim.apply_overrides({{"sw.control", 0.0f}});
    sim.step(1.0f / 60.0f);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);

    sim.apply_overrides({{"sw.control", 1.0f}});
    sim.step(1.0f / 60.0f);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 28.0f, 1e-3f);

    sim.apply_overrides({{"sw.control", 0.0f}});
    sim.step(1.0f / 60.0f);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);
}

TEST(PushRuntime, InitialValuesSeedState) {
    const std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery", "params": {"v_nominal": "24.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"}
        ],
        "initial_values": {
            "bat.v_out": 11.5
        }
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    EXPECT_NEAR(sim.get_port_value("bat", "v_out"), 11.5f, 1e-5f);
    sim.step(1.0f / 60.0f);
    EXPECT_TRUE(std::isfinite(sim.get_port_value("bat", "v_out")));
}

TEST(PushRuntime, SourceConflictErrorMessageReadable) {
    std::vector<DeviceInstance> devices = {
        make_device("b1", "Battery", {{"v_nominal", "28.0"}}),
        make_device("b2", "Battery", {{"v_nominal", "27.0"}}),
        make_device("gnd", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"b1.v_out", "b2.v_out"},
        {"gnd.v", "b1.v_in"},
        {"gnd.v", "b2.v_in"}
    };

    try {
        (void)build_systems_dev(devices, connections);
        FAIL() << "Expected source conflict to throw";
    }
    catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("b1.v_out"), std::string::npos);
        EXPECT_NE(msg.find("b2.v_out"), std::string::npos);
        EXPECT_NE(msg.find("signal"), std::string::npos);
    }
}

TEST(PushRuntime, GS24StartupSequenceProducesOutput) {
    const std::string json = R"({
        "devices": [
            {"name": "gs", "classname": "GS24", "params": {"target_rpm": "60.0", "v_nominal": "28.5"}},
            {"name": "k", "classname": "RefNode", "params": {"value": "1.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "k.v", "to": "gs.k_mod"},
            {"from": "gnd.v", "to": "gs.v_in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    for (int i = 0; i < 90; ++i) {
        sim.step(1.0f / 60.0f);
    }

    const float v_out = sim.get_port_value("gs", "v_out");
    EXPECT_TRUE(std::isfinite(v_out));
    EXPECT_GT(v_out, 0.1f);
}

TEST(PushRuntime, RU19ASmokeNoNaNOverLongRun) {
    const std::string json = R"({
        "devices": [
            {"name": "ru", "classname": "RU19A", "params": {"target_rpm": "5000.0"}},
            {"name": "k", "classname": "RefNode", "params": {"value": "1.0"}},
            {"name": "s", "classname": "RefNode", "params": {"value": "28.0"}}
        ],
        "connections": [
            {"from": "k.v", "to": "ru.k_mod"},
            {"from": "s.v", "to": "ru.v_start"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    for (int i = 0; i < 240; ++i) {
        sim.step(1.0f / 60.0f);
    }

    EXPECT_TRUE(std::isfinite(sim.get_port_value("ru", "rpm_out")));
    EXPECT_TRUE(std::isfinite(sim.get_port_value("ru", "t4_out")));
    EXPECT_TRUE(std::isfinite(sim.get_port_value("ru", "v_bus")));
}

TEST(PushRuntime, DynamicFeedbackLoopStableAndBounded) {
    const std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery", "params": {"v_nominal": "28.0"}},
            {"name": "sw", "classname": "Switch", "params": {"closed": "false"}},
            {"name": "cmp", "classname": "Comparator"},
            {"name": "ref_hi", "classname": "RefNode", "params": {"value": "14.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "sw.v_in"},
            {"from": "sw.v_out", "to": "cmp.Vb"},
            {"from": "ref_hi.v", "to": "cmp.Va"},
            {"from": "cmp.o", "to": "sw.control"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    for (int i = 0; i < 180; ++i) {
        sim.step(1.0f / 60.0f);
        const float v = sim.get_port_value("sw", "v_out");
        EXPECT_TRUE(std::isfinite(v));
        EXPECT_GE(v, -0.1f);
        EXPECT_LE(v, 28.1f);
    }
}
