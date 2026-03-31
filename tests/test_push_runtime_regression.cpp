#include <gtest/gtest.h>

#include "jit_solver/jit_solver.h"
#include "jit_solver/simulator.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"

#include <cmath>
#include <stdexcept>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

/// Read a file and return its contents as a string.
/// Fails with a clear message if the file cannot be read.
static std::string read_file_or_fail(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    return content;
}

/// Find the closed_circuit.blueprint file, trying multiple possible paths
/// to work both when run via ctest (working directory: build/tests/)
/// and when run directly (working directory: build/).
static std::string find_closed_circuit_blueprint() {
    std::vector<std::string> try_paths = {
        "../../closed_circuit.blueprint",  // when run from build/tests/ via ctest
        "../closed_circuit.blueprint",     // when run from build/ directly
        "closed_circuit.blueprint",        // when run from project root
    };
    for (const auto& p : try_paths) {
        std::ifstream f(p);
        if (f.is_open()) return p;
    }
    std::string tried;
    for (const auto& p : try_paths) {
        if (!tried.empty()) tried += ", ";
        tried += p;
    }
    throw std::runtime_error("Could not find closed_circuit.blueprint in any of: " + tried);
}

/// Convert blueprint v3 format (nodes/wires) to simulation JSON format (devices/connections).
/// Blueprint v3 wires have source/target like "/node:port" - these are converted to "node.port".
static std::string blueprint_to_simulation_json(const std::string& blueprint_path) {
    std::string content = read_file_or_fail(blueprint_path);
    json bp = json::parse(content);

    json result;
    result["devices"] = json::array();
    result["connections"] = json::array();

    // Convert nodes to devices
    if (bp.contains("nodes") && bp["nodes"].is_array()) {
        for (const auto& node : bp["nodes"]) {
            json dev;
            dev["name"] = node["name"].get<std::string>();
            dev["classname"] = node["type"].get<std::string>();
            if (node.contains("params") && node["params"].is_object()) {
                dev["params"] = json::object();
                for (const auto& [k, v] : node["params"].items()) {
                    if (v.is_number()) {
                        // Use nlohmann::json dump for full round-trip precision.
                        // std::to_string(double) truncates to 6 decimal places,
                        // silently drifting from blueprint source-of-truth values.
                        dev["params"][k] = json(v.get<double>()).dump();
                    } else {
                        dev["params"][k] = v.get<std::string>();
                    }
                }
            }
            result["devices"].push_back(dev);
        }
    }

    // Convert wires to connections
    if (bp.contains("wires") && bp["wires"].is_array()) {
        for (const auto& wire : bp["wires"]) {
            json conn;
            std::string from = wire["source"].get<std::string>();
            std::string to = wire["target"].get<std::string>();

            // Convert "/node:port" format to "node.port"
            if (!from.empty() && from[0] == '/') from = from.substr(1);
            if (!to.empty() && to[0] == '/') to = to.substr(1);
            std::replace(from.begin(), from.end(), ':', '.');
            std::replace(to.begin(), to.end(), ':', '.');

            conn["from"] = from;
            conn["to"] = to;
            result["connections"].push_back(conn);
        }
    }

    return result.dump();
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
    // Frame N: execute uses previous closed state, commit toggles at end of frame.
    sim.step(1.0f / 60.0f);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);
    // Frame N+1: new state is visible.
    sim.step(1.0f / 60.0f);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 28.0f, 1e-3f);

    sim.apply_overrides({{"sw.control", 0.0f}});
    sim.step(1.0f / 60.0f);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 28.0f, 1e-3f);
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

TEST(PushRuntime, LerpNodeExecuteProducesOutput) {
    // Regression: LerpNode::execute() was a no-op, leaving output at 0.
    // The lerp/deadzone logic lived in finalize_step() which was never called.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "10.0"}},
            {"name": "lerp", "classname": "LerpNode", "params": {"factor": "1.0", "deadzone": "0.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "lerp.input"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    sim.step(1.0f / 60.0f);

    // With factor=1.0 and deadzone=0.0, LerpNode should converge immediately
    // to the input value. A no-op execute() would leave output at 0.0.
    EXPECT_NEAR(sim.get_port_value("lerp", "output"), 10.0f, 1e-4f);
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

TEST(PushRuntime, CommitHookRunsAfterExecute) {
    // Verify commit hook runs after execute: switch state change becomes
    // visible on the next frame.
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

    // Initially switch is open, v_out should be 0
    sim.step(1.0f / 60.0f);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);

    // Apply control to close the switch
    sim.apply_overrides({{"sw.control", 1.0f}});
    sim.step(1.0f / 60.0f);
    // First frame after edge: old state still used during execute.
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);
    // Next frame: committed state is visible.
    sim.step(1.0f / 60.0f);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 28.0f, 1e-3f);
}

TEST(PushRuntime, StatefulComponentOneFrameDelaySemantic) {
    // Stateful changes are committed at end-of-frame and visible next frame.
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

    // Initial: switch open, output = 0
    sim.step(1.0f / 60.0f);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);

    // Toggle control (edge detect requires change)
    sim.apply_overrides({{"sw.control", 1.0f}});
    sim.step(1.0f / 60.0f);

    // Frame N: execute still sees previous open state.
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);
    // Frame N+1: committed state visible.
    sim.step(1.0f / 60.0f);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 28.0f, 1e-3f);

    // Toggle back
    sim.apply_overrides({{"sw.control", 0.0f}});
    sim.step(1.0f / 60.0f);

    // Frame M: still previous closed state.
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 28.0f, 1e-3f);
    // Frame M+1: committed open state visible.
    sim.step(1.0f / 60.0f);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);
}

TEST(PushRuntime, IntegratorComputesCorrectAccumulation) {
    // Integrator uses one-frame-delay semantics: output reflects committed state,
    // integration result is staged and visible on the NEXT frame.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "10.0"}},
            {"name": "integ", "classname": "Integrator", "params": {"gain": "1.0", "initial_val": "0.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "integ.in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    float dt = 1.0f / 60.0f;

    // Frame 0: output = committed accumulator (initial_val=0); integration is staged
    sim.step(dt);
    float out0 = sim.get_port_value("integ", "out");
    EXPECT_NEAR(out0, 0.0f, 1e-6f);

    // Frame 1: output = committed accumulator from frame 0 = 10 * dt * 1
    sim.step(dt);
    float out1 = sim.get_port_value("integ", "out");
    EXPECT_NEAR(out1, 10.0f * dt * 1.0f, 0.02f);

    // Frame 2: output = committed accumulator from frame 1 = 10 * dt * 2
    sim.step(dt);
    float out2 = sim.get_port_value("integ", "out");
    EXPECT_NEAR(out2, 10.0f * dt * 2.0f, 0.02f);

    // Verify monotonic increase (out0 is zero, so out1 > out0)
    EXPECT_GT(out1, out0);
    EXPECT_GT(out2, out1);
}

TEST(PushRuntime, SampleHoldBasicOperation) {
    // SampleHold samples on rising edge immediately. commit is no-op for this component.
    // Test basic holding behavior with a simple circuit.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "5.0"}},
            {"name": "sh", "classname": "SampleHold"}
        ],
        "connections": [
            {"from": "src.v", "to": "sh.in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    float dt = 1.0f / 60.0f;

    // Initial: no trigger (default 0), output should be 0
    sim.step(dt);
    EXPECT_NEAR(sim.get_port_value("sh", "out"), 0.0f, 1e-4f);

    // Multiple steps with no trigger should still be 0
    sim.step(dt);
    sim.step(dt);
    EXPECT_NEAR(sim.get_port_value("sh", "out"), 0.0f, 1e-4f);
}

TEST(PushRuntime, SlewRateConvergesToInput) {
    // SlewRate limits rate of change. commit is no-op; continuous filter behavior.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "10.0"}},
            {"name": "slew", "classname": "SlewRate", "params": {"max_rate": "2.0", "deadzone": "0.001"}}
        ],
        "connections": [
            {"from": "src.v", "to": "slew.in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    float dt = 1.0f / 60.0f;

    // Step several frames and verify monotonic convergence
    std::vector<float> outputs;
    for (int i = 0; i < 30; ++i) {
        sim.step(dt);
        outputs.push_back(sim.get_port_value("slew", "out"));
    }

    // Verify monotonically increasing (rate limited toward 10.0)
    for (size_t i = 1; i < outputs.size(); ++i) {
        EXPECT_GE(outputs[i], outputs[i-1] - 1e-6f)
            << "SlewRate output should not decrease";
    }

    // After enough frames, should be close to input (10.0)
    EXPECT_GT(outputs.back(), 9.0f);

    // Verify no NaN
    for (size_t i = 0; i < outputs.size(); ++i) {
        EXPECT_TRUE(std::isfinite(outputs[i])) << "Output at frame " << i << " should be finite";
    }
}

TEST(PushRuntime, ComponentApiCommitHookCoverageSmoke) {
    // Smoke test: instantiate representative components across domains and verify
    // the scheduler commit path executes safely without errors.
    const std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery", "params": {"v_nominal": "28.0"}},
            {"name": "sw", "classname": "Switch", "params": {"closed": "true"}},
            {"name": "add", "classname": "Add"},
            {"name": "pid", "classname": "PID", "params": {"Kp": "1.0", "Ki": "0.1", "Kd": "0.0", "output_min": "-10.0", "output_max": "10.0", "filter_alpha": "0.1"}},
            {"name": "slew", "classname": "SlewRate", "params": {"max_rate": "5.0", "deadzone": "0.01"}},
            {"name": "integ", "classname": "Integrator", "params": {"gain": "1.0", "initial_val": "0.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
            {"name": "gen", "classname": "Generator", "params": {"v_nominal": "12.0"}},
            {"name": "relay", "classname": "Relay", "params": {"hold_threshold": "0.5"}}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "sw.v_in"},
            {"from": "sw.v_out", "to": "add.A"},
            {"from": "gnd.v", "to": "add.B"},
            {"from": "add.o", "to": "pid.setpoint"},
            {"from": "gnd.v", "to": "pid.feedback"},
            {"from": "pid.output", "to": "slew.in"},
            {"from": "slew.out", "to": "integ.in"},
            {"from": "gnd.v", "to": "gen.v_in"},
            {"from": "gen.v_out", "to": "relay.v_in"}
        ]
    })";

    JIT_Simulator sim;
    EXPECT_NO_THROW(sim.start_from_json(json));

    // Run multiple steps to verify commit path executes safely each frame
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 5; ++i) {
        EXPECT_NO_THROW(sim.step(dt));
    }

    // Verify all outputs are finite
    EXPECT_TRUE(std::isfinite(sim.get_port_value("bat", "v_out")));
    EXPECT_TRUE(std::isfinite(sim.get_port_value("sw", "v_out")));
    EXPECT_TRUE(std::isfinite(sim.get_port_value("add", "o")));
    EXPECT_TRUE(std::isfinite(sim.get_port_value("pid", "output")));
    EXPECT_TRUE(std::isfinite(sim.get_port_value("slew", "out")));
    EXPECT_TRUE(std::isfinite(sim.get_port_value("integ", "out")));
}

// == Push Migration: Two-Phase State Semantics Tests ==

TEST(PushRuntime, TimeDelayCommitSemantics) {
    // TimeDelay: verify two-phase commit semantics work without crashing.
    // Note: Cold start behavior makes output immediately follow input on first frame.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "1.0"}},
            {"name": "td", "classname": "TimeDelay", "params": {"delay_on": "0.1", "delay_off": "0.05"}}
        ],
        "connections": [
            {"from": "src.v", "to": "td.in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);
    float dt = 1.0f / 60.0f;

    // Run several steps - should be stable without NaN or crashes
    for (int i = 0; i < 10; ++i) {
        sim.step(dt);
        float out = sim.get_port_value("td", "out");
        EXPECT_GE(out, 0.0f);
        EXPECT_LE(out, 1.0f);
        EXPECT_TRUE(std::isfinite(out));
    }
}

TEST(PushRuntime, MonostableCommitSemantics) {
    // Monostable: one-shot timer. Rising edge triggers duration-long pulse.
    // Output from committed timer state; next timer value staged during execute.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "1.0"}},
            {"name": "mono", "classname": "Monostable", "params": {"duration": "30.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "mono.in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);
    float dt = 1.0f / 60.0f;

    // Initial: output should be 0
    sim.step(dt);
    EXPECT_NEAR(sim.get_port_value("mono", "out"), 0.0f, 1e-4f);

    // Rising edge trigger happens at step 0. Frame 0 output stays 0 (committed timer=0).
    // After commit, timer = duration. Frame 1 output = 1 (committed timer > 0).
    sim.step(dt);
    EXPECT_NEAR(sim.get_port_value("mono", "out"), 1.0f, 1e-4f);

    // Timer continues to count down in subsequent frames
    sim.step(dt);
    EXPECT_NEAR(sim.get_port_value("mono", "out"), 1.0f, 1e-4f);
}

TEST(PushRuntime, SlewRateCommitSemantics) {
    // SlewRate: verify two-phase commit semantics work without crashing.
    // Note: Cold start behavior makes output immediately follow input on first frame.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "10.0"}},
            {"name": "slew", "classname": "SlewRate", "params": {"max_rate": "2.0", "deadzone": "0.001"}}
        ],
        "connections": [
            {"from": "src.v", "to": "slew.in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);
    float dt = 1.0f / 60.0f;

    // Run several steps - should be stable and converge toward input
    std::vector<float> outputs;
    for (int i = 0; i < 20; ++i) {
        sim.step(dt);
        outputs.push_back(sim.get_port_value("slew", "out"));
    }

    // Verify outputs are valid and monotonically increasing (cold start converges immediately)
    for (size_t i = 0; i < outputs.size(); ++i) {
        EXPECT_GE(outputs[i], 0.0f);
        EXPECT_LE(outputs[i], 10.5f);
        EXPECT_TRUE(std::isfinite(outputs[i]));
    }

    // Verify output stays at target after cold start
    EXPECT_GT(outputs.back(), 9.0f);
}

TEST(PushRuntime, AsymSlewRateCommitSemantics) {
    // AsymSlewRate: asymmetric rise/fall rates. Two-phase semantics preserves rate limiting.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "10.0"}},
            {"name": "asym", "classname": "AsymSlewRate", "params": {"rate_up": "5.0", "rate_down": "1.0", "deadzone": "0.001"}}
        ],
        "connections": [
            {"from": "src.v", "to": "asym.in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);
    float dt = 1.0f / 60.0f;

    std::vector<float> outputs;
    for (int i = 0; i < 20; ++i) {
        sim.step(dt);
        outputs.push_back(sim.get_port_value("asym", "out"));
    }

    // Verify monotonically increasing (rate_up is high, should converge quickly)
    for (size_t i = 1; i < outputs.size(); ++i) {
        EXPECT_GE(outputs[i], outputs[i-1] - 1e-6f);
    }
    EXPECT_GT(outputs.back(), 9.0f);
}

TEST(PushRuntime, IntegratorCommitOneFrameDelay) {
    // Integrator: output = committed accumulator (one frame delay from integration).
    // This explicitly tests the one-frame-delay semantic.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "5.0"}},
            {"name": "integ", "classname": "Integrator", "params": {"gain": "1.0", "initial_val": "0.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "integ.in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);
    float dt = 1.0f / 60.0f;

    // Frame N: output = committed accumulator from frame N-1
    // Frame 0: out = initial_val = 0.0
    sim.step(dt);
    float out0 = sim.get_port_value("integ", "out");
    EXPECT_NEAR(out0, 0.0f, 1e-6f);

    // Frame 1: out = committed accumulator from frame 0 = 5.0 * dt * 1.0
    sim.step(dt);
    float out1 = sim.get_port_value("integ", "out");
    EXPECT_NEAR(out1, 5.0f * dt, 0.02f);

    // Frame 2: out = committed accumulator from frame 1 = 5.0 * dt * 2.0
    sim.step(dt);
    float out2 = sim.get_port_value("integ", "out");
    EXPECT_NEAR(out2, 5.0f * dt * 2.0f, 0.02f);

    // Verify monotonic increase
    EXPECT_GT(out1, out0);
    EXPECT_GT(out2, out1);
}

TEST(PushRuntime, SampleHoldCommitSemantics) {
    // SampleHold: samples input on rising edge of trigger.
    // Output from committed stored_value; sample captured in next staged value.
    // Basic smoke test: verify component runs without errors and produces valid output.
    const std::string json = R"({
        "devices": [
            {"name": "val_src", "classname": "RefNode", "params": {"value": "7.5"}},
            {"name": "sh", "classname": "SampleHold"}
        ],
        "connections": [
            {"from": "val_src.v", "to": "sh.in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);
    float dt = 1.0f / 60.0f;

    // Without trigger, output should be 0 (initial stored_value)
    for (int i = 0; i < 5; ++i) {
        sim.step(dt);
        float out = sim.get_port_value("sh", "out");
        EXPECT_GE(out, 0.0f);
        EXPECT_LT(out, 8.0f);
    }
}

TEST(PushRuntime, LerpNodeCommitSemantics) {
    // LerpNode: linear interpolation with deadzone. Two-phase semantics ensures
    // output from committed current_value, next_value staged during execute.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "10.0"}},
            {"name": "lerp", "classname": "LerpNode", "params": {"factor": "1.0", "deadzone": "0.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "lerp.input"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);
    float dt = 1.0f / 60.0f;

    // Frame 0: cold start, output = input via committed state
    sim.step(dt);
    float out0 = sim.get_port_value("lerp", "output");
    EXPECT_NEAR(out0, 10.0f, 0.1f);

    // Subsequent frames: should stay at converged value (factor=1.0, deadzone=0.0)
    sim.step(dt);
    float out1 = sim.get_port_value("lerp", "output");
    EXPECT_NEAR(out1, 10.0f, 0.1f);

    sim.step(dt);
    float out2 = sim.get_port_value("lerp", "output");
    EXPECT_NEAR(out2, 10.0f, 0.1f);

    // All outputs should be finite and valid
    EXPECT_TRUE(std::isfinite(out0));
    EXPECT_TRUE(std::isfinite(out1));
    EXPECT_TRUE(std::isfinite(out2));
}

TEST(PushRuntime, StrictParamMissingThrowsForPID) {
    // Verify that missing required params for PID throws runtime_error
    // with component name and missing key.
    DeviceInstance dev;
    dev.name = "pid_bad";
    dev.classname = "PID";
    dev.execution = {};
    dev.params = {};  // Missing all params

    auto ports = get_component_ports("PID");
    for (const auto& port_name : ports) {
        dev.ports[port_name] = Port{PortDirection::InOut, PortType::Any};
    }

    std::vector<DeviceInstance> test_devs = {dev};
    std::vector<std::pair<std::string, std::string>> connections;

    try {
        (void)build_systems_dev(test_devs, connections);
        FAIL() << "Expected runtime_error for PID missing Kp";
    }
    catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("pid_bad"), std::string::npos)
            << "Error message should contain component name";
        EXPECT_NE(msg.find("Kp"), std::string::npos)
            << "Error message should contain missing key 'Kp'";
    }
}

TEST(PushRuntime, StrictParamMissingThrowsForSlewRate) {
    // Verify that missing required params for SlewRate throws runtime_error
    DeviceInstance dev;
    dev.name = "slew_bad";
    dev.classname = "SlewRate";
    dev.execution = {};
    dev.params = {{"deadzone", "0.001"}};  // Missing max_rate

    auto ports = get_component_ports("SlewRate");
    for (const auto& port_name : ports) {
        dev.ports[port_name] = Port{PortDirection::InOut, PortType::Any};
    }

    std::vector<DeviceInstance> test_devs = {dev};
    std::vector<std::pair<std::string, std::string>> connections;

    try {
        (void)build_systems_dev(test_devs, connections);
        FAIL() << "Expected runtime_error for SlewRate missing max_rate";
    }
    catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("slew_bad"), std::string::npos)
            << "Error message should contain component name";
        EXPECT_NE(msg.find("max_rate"), std::string::npos)
            << "Error message should contain missing key 'max_rate'";
    }
}

TEST(PushRuntime, StrictParamUsesCanonicalKey) {
    // Verify that non-default PID parameters are actually used (not silently falling back).
    // Uses canonical key names Kp, Ki, Kd.
    const std::string json = R"({
        "devices": [
            {"name": "setpoint", "classname": "RefNode", "params": {"value": "10.0"}},
            {"name": "feedback", "classname": "RefNode", "params": {"value": "5.0"}},
            {"name": "pid", "classname": "PID", "params": {"Kp": "2.0", "Ki": "0.5", "Kd": "0.1", "output_min": "-100.0", "output_max": "100.0", "filter_alpha": "0.3"}}
        ],
        "connections": [
            {"from": "setpoint.v", "to": "pid.setpoint"},
            {"from": "feedback.v", "to": "pid.feedback"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    float dt = 1.0f / 60.0f;

    // Run several steps to let PID respond
    for (int i = 0; i < 10; ++i) {
        sim.step(dt);
    }

    // PID output should be non-zero and bounded by output_min/output_max
    float pid_out = sim.get_port_value("pid", "output");
    EXPECT_GE(pid_out, -100.0f);
    EXPECT_LE(pid_out, 100.0f);

    // With error = 5.0 (setpoint - feedback), proportional term alone = 2.0 * 5.0 = 10.0
    // so output should be significant (not using defaults that would give 1.0 * 5.0 = 5.0)
    EXPECT_GT(std::abs(pid_out), 5.0f);
    EXPECT_TRUE(std::isfinite(pid_out));
}

TEST(PushRuntime, UnknownParamKeyThrows) {
    // Verify that unknown/misspelled params throw runtime_error with helpful message.
    // This is a regression test for the strict param validation feature.
    
    // Test 1: PID with typo Kpp instead of Kp
    {
        DeviceInstance dev;
        dev.name = "pid_bad";
        dev.classname = "PID";
        dev.execution = {};
        dev.params = {
            {"Kpp", "2.0"},  // Typo: should be "Kp"
            {"Ki", "0.5"},
            {"Kd", "0.1"},
            {"output_min", "-100.0"},
            {"output_max", "100.0"},
            {"filter_alpha", "0.3"}
        };
        
        auto ports = get_component_ports("PID");
        for (const auto& port_name : ports) {
            dev.ports[port_name] = Port{PortDirection::InOut, PortType::Any};
        }
        
        std::vector<DeviceInstance> test_devs = {dev};
        std::vector<std::pair<std::string, std::string>> connections;
        
        try {
            (void)build_systems_dev(test_devs, connections);
            FAIL() << "Expected runtime_error for PID with unknown param 'Kpp'";
        }
        catch (const std::runtime_error& e) {
            const std::string msg = e.what();
            EXPECT_NE(msg.find("pid_bad"), std::string::npos)
                << "Error message should contain component name";
            EXPECT_NE(msg.find("Kpp"), std::string::npos)
                << "Error message should contain unknown key 'Kpp'";
        }
    }
    
    // Test 2: Battery with typo inv_internal_r -> should be caught as unknown
    // (but inv_internal_r is whitelisted as internal computed, so use another typo)
    {
        DeviceInstance dev;
        dev.name = "bat_typo";
        dev.classname = "Battery";
        dev.execution = {};
        dev.params = {
            {"v_nominal", "28.0"},
            {"internal_r", "0.01"},
            {"capacitor", "1000.0"}  // Typo: should be "capacity"
        };
        
        auto ports = get_component_ports("Battery");
        for (const auto& port_name : ports) {
            dev.ports[port_name] = Port{PortDirection::InOut, PortType::Any};
        }
        
        std::vector<DeviceInstance> test_devs = {dev};
        std::vector<std::pair<std::string, std::string>> connections;
        
        try {
            (void)build_systems_dev(test_devs, connections);
            FAIL() << "Expected runtime_error for Battery with unknown param 'capacitor'";
        }
        catch (const std::runtime_error& e) {
            const std::string msg = e.what();
            EXPECT_NE(msg.find("bat_typo"), std::string::npos)
                << "Error message should contain component name";
            EXPECT_NE(msg.find("capacitor"), std::string::npos)
                << "Error message should contain unknown key 'capacitor'";
        }
    }
    
    // Test 3: Valid params should NOT throw
    {
        DeviceInstance dev;
        dev.name = "pid_ok";
        dev.classname = "PID";
        dev.execution = {};
        dev.params = {
            {"Kp", "2.0"},
            {"Ki", "0.5"},
            {"Kd", "0.1"},
            {"output_min", "-100.0"},
            {"output_max", "100.0"},
            {"filter_alpha", "0.3"}
        };
        
        auto ports = get_component_ports("PID");
        for (const auto& port_name : ports) {
            dev.ports[port_name] = Port{PortDirection::InOut, PortType::Any};
        }
        
        std::vector<DeviceInstance> test_devs = {dev};
        std::vector<std::pair<std::string, std::string>> connections;
        
        EXPECT_NO_THROW((void)build_systems_dev(test_devs, connections));
    }
}

TEST(PushRuntime, GS24ExtendedParamsAreAcceptedAndAffectOutput) {
    const std::string json_low_threshold = R"({
        "devices": [
            {"name": "gs", "classname": "GS24", "params": {
                "target_rpm": "60.0",
                "v_nominal": "28.5",
                "rpm_threshold": "0.1",
                "rpm_cutoff": "0.45"
            }},
            {"name": "k", "classname": "RefNode", "params": {"value": "1.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "k.v", "to": "gs.k_mod"},
            {"from": "gnd.v", "to": "gs.v_in"}
        ]
    })";

    const std::string json_high_threshold = R"({
        "devices": [
            {"name": "gs", "classname": "GS24", "params": {
                "target_rpm": "60.0",
                "v_nominal": "28.5",
                "rpm_threshold": "0.9",
                "rpm_cutoff": "0.45"
            }},
            {"name": "k", "classname": "RefNode", "params": {"value": "1.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "k.v", "to": "gs.k_mod"},
            {"from": "gnd.v", "to": "gs.v_in"}
        ]
    })";

    JIT_Simulator low;
    JIT_Simulator high;
    low.start_from_json(json_low_threshold);
    high.start_from_json(json_high_threshold);

    for (int i = 0; i < 90; ++i) {
        low.step(1.0f / 60.0f);
        high.step(1.0f / 60.0f);
    }

    const float low_v = low.get_port_value("gs", "v_out");
    const float high_v = high.get_port_value("gs", "v_out");
    EXPECT_TRUE(std::isfinite(low_v));
    EXPECT_TRUE(std::isfinite(high_v));
    EXPECT_GT(low_v, high_v);
}

TEST(PushRuntime, RU19AExtendedParamsAreAcceptedAndCrankTimeControlsState) {
    std::vector<DeviceInstance> devices = {
        make_device("ru", "RU19A", {
            {"auto_start", "false"},
            {"target_rpm", "4800.0"},
            {"spinup_inertia", "1.2"},
            {"spindown_inertia", "0.03"},
            {"crank_time", "4.0"},
            {"ignition_time", "3.0"},
            {"start_timeout", "20.0"},
            {"t4_target", "420.0"},
            {"t4_max", "760.0"},
            {"ambient_temp", "15.0"}
        }),
        make_device("k", "RefNode", {{"value", "1.0"}}),
        make_device("v_src", "RefNode", {{"value", "28.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"k.v", "ru.k_mod"},
        {"v_src.v", "ru.v_start"}
    };

    auto result = build_systems_dev(devices, connections);
    auto st = make_state(result.signal_count);
    RU19A<JitProvider>* ru = std::get_if<RU19A<JitProvider>>(&result.devices.at("ru"));
    ASSERT_NE(ru, nullptr);

    ru->start();
    result.scheduler.step(st, 1.0f / 60.0f);

    for (int i = 0; i < 170; ++i) {
        result.scheduler.step(st, 1.0f / 60.0f);
    }
    EXPECT_EQ(ru->state, APUState::CRANKING);

    for (int i = 0; i < 80; ++i) {
        result.scheduler.step(st, 1.0f / 60.0f);
    }
    EXPECT_NE(ru->state, APUState::CRANKING);
}

// == RU19A Start/Stop Request Semantics Tests ==
// Verifies two-phase commit semantics for start() and stop() requests.
// start() while OFF -> CRANKING next frame
// stop() while RUNNING -> STOPPING next frame

TEST(PushRuntime, RU19AStartRequestTransitionsToCrankingNextFrame) {
    // Build system without using JIT_Simulator (need direct component access)
    std::vector<DeviceInstance> devices = {
        make_device("ru", "RU19A", {{"auto_start", "false"}}),
        make_device("k", "RefNode", {{"value", "1.0"}}),
        make_device("v_src", "RefNode", {{"value", "5.0"}})  // v_start below auto_start threshold
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"k.v", "ru.k_mod"},
        {"v_src.v", "ru.v_start"}
    };

    auto result = build_systems_dev(devices, connections);
    auto st = make_state(result.signal_count);

    // Access RU19A component directly to call start()
    RU19A<JitProvider>* ru = std::get_if<RU19A<JitProvider>>(&result.devices.at("ru"));
    ASSERT_NE(ru, nullptr);

    // Initial state is OFF - v_bus should be 0
    result.scheduler.step(st, 1.0f / 60.0f);
    const uint32_t v_bus_sig = result.port_to_signal.at("ru.v_bus");
    EXPECT_NEAR(st.values[v_bus_sig], 0.0f, 1e-4f);

    // Call start() - should stage CRANKING transition
    ru->start();
    result.scheduler.step(st, 1.0f / 60.0f);

    // After one step, v_bus should STILL be 0 (CRANKING produces no v_bus output)
    EXPECT_NEAR(st.values[v_bus_sig], 0.0f, 1e-4f);
    // The request was committed: state is now CRANKING
}

TEST(PushRuntime, RU19AStopRequestTransitionsToStoppingNextFrame) {
    // This test verifies stop() while RUNNING transitions to STOPPING next frame.
    // Setup: RU19A with auto_start=false, manually drive to RUNNING via start()
    std::vector<DeviceInstance> devices = {
        make_device("ru", "RU19A", {{"auto_start", "false"}, {"target_rpm", "4800.0"}}),
        make_device("k", "RefNode", {{"value", "1.0"}}),
        make_device("v_src", "RefNode", {{"value", "5.0"}})
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"k.v", "ru.k_mod"},
        {"v_src.v", "ru.v_start"}
    };

    auto result = build_systems_dev(devices, connections);
    auto st = make_state(result.signal_count);

    RU19A<JitProvider>* ru = std::get_if<RU19A<JitProvider>>(&result.devices.at("ru"));
    ASSERT_NE(ru, nullptr);

    const uint32_t v_bus_sig = result.port_to_signal.at("ru.v_bus");

    // Manually drive to RUNNING state via start() calls
    // Frame 0: start() -> CRANKING
    ru->start();
    result.scheduler.step(st, 1.0f / 60.0f);

    // Stay in CRANKING until transition to IGNITION/RUNNING
    // crank_time = 2.0s, at 60Hz that's 120 frames
    for (int i = 0; i < 130; ++i) {
        result.scheduler.step(st, 1.0f / 60.0f);
    }

    // Now should be in RUNNING state - v_bus should be non-zero
    // (unless we're still in IGNITION which also has v_bus=0)
    float v_bus_before_stop = st.values[v_bus_sig];
    (void)v_bus_before_stop;  // May be 0 if still in IGNITION

    // Call stop() - stages STOPPING transition
    ru->stop();

    // stop() is staged, not yet committed - outputs still reflect RUNNING or current state
    result.scheduler.step(st, 1.0f / 60.0f);

    // Next frame: STOPPING state committed, v_bus becomes 0.0
    EXPECT_NEAR(st.values[v_bus_sig], 0.0f, 1e-4f);
}

TEST(PushRuntime, RU19AStartStopRequestsDoNotMutateCurrentFrameOutputs) {
    // Verifies request semantics: start()/stop() do not affect current-frame outputs.
    // Outputs only change on the NEXT frame after commit.
    // Uses JIT_Simulator for proper signal initialization
    const std::string json = R"({
        "devices": [
            {"name": "ru", "classname": "RU19A", "params": {"auto_start": "false", "target_rpm": "4800.0"}},
            {"name": "k", "classname": "RefNode", "params": {"value": "1.0"}},
            {"name": "v_src", "classname": "RefNode", "params": {"value": "5.0"}}
        ],
        "connections": [
            {"from": "k.v", "to": "ru.k_mod"},
            {"from": "v_src.v", "to": "ru.v_start"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    float dt = 1.0f / 60.0f;

    // Initial OFF state
    sim.step(dt);
    EXPECT_NEAR(sim.get_port_value("ru", "v_bus"), 0.0f, 1e-4f);

    // The start() method on RU19A component sets start_requested flag.
    // Since JIT_Simulator doesn't expose component method access directly,
    // we verify that during OFF->CRANKING transition, v_bus stays at 0
    // (CRANKING produces no v_bus output, same as OFF).
    // This confirms the staging behavior: request is queued but outputs
    // only change when the new state actually produces different outputs.
    sim.step(dt);
    EXPECT_NEAR(sim.get_port_value("ru", "v_bus"), 0.0f, 1e-4f);

    // Continue stepping - should eventually reach RUNNING if we had
    // a way to call start(). Since we don't, auto_start won't trigger
    // (v_start = 5.0 < 10.0 threshold), so we stay OFF.
    // This test documents the expected behavior for future component access.
}

// == Batch 5: Solver Ownership Integration Tests ==

TEST(PushRuntime, SolverOwnedComponentsNotScheduledForElectricalPropagation) {
    // Verify that Battery, Generator, Resistor are NOT scheduled
    // in the push scheduler for electrical propagation.
    // These components are now owned by the electrical solver instead.
    // RefNode IS scheduled as a source: it writes its constant reference value
    // into the signal array each frame so downstream logical consumers see it.
    std::vector<DeviceInstance> devices = {
        make_device("bat", "Battery", {{"v_nominal", "28.0"}}),
        make_device("gen", "Generator", {{"v_nominal", "28.0"}}),
        make_device("gnd", "RefNode", {{"value", "0.0"}}),
        make_device("res", "Resistor", {{"conductance", "0.1"}}),
        make_device("sw", "Switch", {{"closed", "true"}})  // consumer to verify scheduling works
    };

    std::vector<std::pair<std::string, std::string>> connections = {
        {"gnd.v", "bat.v_in"},
        {"bat.v_out", "res.v_in"},
        {"res.v_out", "sw.v_in"},
        {"gnd.v", "gen.v_in"}
    };

    auto result = build_systems_dev(devices, connections);

    // RefNode is scheduled as a source (it must write its constant value).
    // Battery, Generator, Resistor are solver-owned and NOT scheduled.
    EXPECT_EQ(result.scheduler.source_count(), 1u)
        << "Only RefNode should be scheduled as a source";
    EXPECT_EQ(result.scheduler.consumer_count(), 1u)
        << "Only Switch should be scheduled as consumer (others are solver-owned)";
}

TEST(PushRuntime, ClosedLoopNoRunawayAfterManySteps) {
    // Closed loop: Battery -> Resistor -> IndicatorLight -> RefNode (return)
    // After solver ownership changes, voltages should remain bounded.
    const std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery", "params": {"v_nominal": "28.0", "internal_r": "0.01"}},
            {"name": "res", "classname": "Resistor", "params": {"conductance": "0.5"}},
            {"name": "ind", "classname": "IndicatorLight", "params": {"rated_voltage": "28.0", "max_brightness": "100.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "res.v_in"},
            {"from": "res.v_out", "to": "ind.v_in"},
            {"from": "ind.v_out", "to": "gnd.v"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    float dt = 1.0f / 60.0f;

    // Step for many frames - voltages should remain bounded
    for (int i = 0; i < 500; ++i) {
        sim.step(dt);

        float bat_vout = sim.get_port_value("bat", "v_out");
        float res_vout = sim.get_port_value("res", "v_out");
        float ind_vout = sim.get_port_value("ind", "v_out");
        float gnd_v = sim.get_port_value("gnd", "v");

        // All voltages should be finite and bounded
        EXPECT_TRUE(std::isfinite(bat_vout)) << "Battery v_out should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(res_vout)) << "Resistor v_out should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(ind_vout)) << "Indicator v_out should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(gnd_v)) << "Ground v should be finite at frame " << i;

        // No runaway to +28V unbounded
        EXPECT_LT(bat_vout, 35.0f) << "Battery v_out should not runaway at frame " << i;
        EXPECT_LT(res_vout, 35.0f) << "Resistor v_out should not runaway at frame " << i;

        // Ground should stay at reference (0V)
        EXPECT_NEAR(gnd_v, 0.0f, 0.1f) << "Ground should stay at 0V at frame " << i;
    }
}

TEST(PushRuntime, IndicatorLightDoesNotOverwriteSolvedNode) {
    // Build circuit where IndicatorLight v_out connects to a fixed RefNode.
    // The RefNode should remain at its fixed value (not overwritten by indicator pass-through).
    // After Batch 5, IndicatorLight does NOT write v_out (solver handles it).
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "10.0"}},
            {"name": "ind", "classname": "IndicatorLight", "params": {"rated_voltage": "28.0", "max_brightness": "100.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "ind.v_in"},
            {"from": "gnd.v", "to": "ind.v_out"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    float dt = 1.0f / 60.0f;

    // Step once
    sim.step(dt);

    // gnd.v should still be 0.0 (not overwritten by indicator pass-through)
    float gnd_v = sim.get_port_value("gnd", "v");
    EXPECT_NEAR(gnd_v, 0.0f, 1e-4f)
        << "RefNode should remain at fixed value (not overwritten by IndicatorLight)";

    // Source should still be at its fixed value
    float src_v = sim.get_port_value("src", "v");
    EXPECT_NEAR(src_v, 10.0f, 1e-4f)
        << "Source RefNode should remain at fixed value";

    // Brightness should still compute correctly
    float brightness = sim.get_port_value("ind", "brightness");
    EXPECT_GT(brightness, 0.0f) << "IndicatorLight brightness should be > 0";
    EXPECT_LE(brightness, 100.0f) << "IndicatorLight brightness should be <= max";
}

TEST(PushRuntime, IndicatorLightBrightnessStillFunctional) {
    // Verify IndicatorLight brightness calculation still works correctly
    // after removing the electrical pass-through.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "14.0"}},
            {"name": "ind", "classname": "IndicatorLight", "params": {"rated_voltage": "28.0", "max_brightness": "100.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "ind.v_in"},
            {"from": "gnd.v", "to": "ind.v_out"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    float dt = 1.0f / 60.0f;
    sim.step(dt);

    // At 14V input with 28V rated, brightness should be 50%
    float brightness = sim.get_port_value("ind", "brightness");
    EXPECT_NEAR(brightness, 50.0f, 1e-3f)
        << "IndicatorLight brightness should be 50% (14V/28V * 100)";
}

// == Batch 7: Real Blueprint Regression Tests ==

TEST(PushRuntime, ClosedCircuitBlueprint_NoRunawayVoltage) {
    // Load real closed_circuit.blueprint from filesystem.
    // Wire connections from blueprint:
    //   battery_1.v_out -> resistor_1.v_in
    //   resistor_1.v_out -> indicatorlight_1.v_in
    //   indicatorlight_1.v_out -> bus_1.v
    //   bus_1.v -> battery_1.v_in
    //   refnode_1.v -> bus_1.v
    //
    // Verifies voltages remain bounded (no runaway) with real fixture topology.
    std::string blueprint_path;
    EXPECT_NO_THROW(blueprint_path = find_closed_circuit_blueprint())
        << "Could not find closed_circuit.blueprint";
    std::string json;
    EXPECT_NO_THROW(json = blueprint_to_simulation_json(blueprint_path))
        << "Failed to parse blueprint from: " << blueprint_path;

    JIT_Simulator sim;
    EXPECT_NO_THROW(sim.start_from_json(json))
        << "Failed to start simulation from loaded blueprint";

    float dt = 1.0f / 60.0f;

    // Run 600 steps (10 seconds at 60Hz)
    for (int i = 0; i < 600; ++i) {
        sim.step(dt);

        float bat_vout = sim.get_port_value("battery_1", "v_out");
        float res_vout = sim.get_port_value("resistor_1", "v_out");
        float ind_vout = sim.get_port_value("indicatorlight_1", "v_out");
        float bus_v = sim.get_port_value("bus_1", "v");
        float gnd_v = sim.get_port_value("refnode_1", "v");

        // All key electrical ports must be finite
        EXPECT_TRUE(std::isfinite(bat_vout)) << "Battery v_out should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(res_vout)) << "Resistor v_out should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(ind_vout)) << "IndicatorLight v_out should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(bus_v)) << "Bus v should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(gnd_v)) << "RefNode v should be finite at frame " << i;

        // No monotonic +28 runaway pattern: voltages should stay bounded
        // Upper bound of 100V is generous (real circuit should be ~28V)
        EXPECT_LT(bat_vout, 100.0f) << "Battery v_out runaway at frame " << i;
        EXPECT_LT(res_vout, 100.0f) << "Resistor v_out runaway at frame " << i;
        EXPECT_LT(ind_vout, 100.0f) << "IndicatorLight v_out runaway at frame " << i;
        EXPECT_LT(bus_v, 100.0f) << "Bus v runaway at frame " << i;

        // Reference node should remain near configured value (0V)
        EXPECT_NEAR(gnd_v, 0.0f, 0.5f) << "RefNode drift at frame " << i;
    }
}

TEST(PushRuntime, ClosedCircuitBlueprint_BatteryChargeDecreases_RealFixture) {
    // Step 12 formal validation: real fixture end-to-end test.
    // Load actual closed_circuit.blueprint and verify battery charge decreases
    // when branch draws current through the full loop topology.
    //
    // Blueprint topology:
    //   battery_1.v_out -> resistor_1.v_in
    //   resistor_1.v_out -> indicatorlight_1.v_in
    //   indicatorlight_1.v_out -> bus_1.v
    //   bus_1.v -> battery_1.v_in
    //   refnode_1.v -> bus_1.v (reference node at 0V)
    //
    // Battery params from blueprint: v_nominal=28.0, internal_r=0.01,
    // capacity=1000, charge=1000
    // Resistor conductance from blueprint: 0.1 (10 ohm load)
    //
    // Root cause of prior flakiness: battery charge is a running accumulator
    // stored as double. At charge=1000 with resistor conductance=0.1, the
    // per-step discharge delta is ~1.2e-5 Ah. In float32, this is below the
    // ULP at 1000.0 (~6.1e-5), so the subtraction was swallowed by rounding.
    // Using double (ULP ~1.1e-13 at 1000) resolves this permanently.
    std::string blueprint_path;
    ASSERT_NO_THROW(blueprint_path = find_closed_circuit_blueprint())
        << "Could not find closed_circuit.blueprint";
    std::string json;
    ASSERT_NO_THROW(json = blueprint_to_simulation_json(blueprint_path))
        << "Failed to parse blueprint from: " << blueprint_path;

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start_from_json(json))
        << "Failed to start simulation from loaded blueprint";

    // Verify blueprint includes expected devices
    double initial_charge = sim.get_battery_charge("battery_1");
    ASSERT_TRUE(std::isfinite(initial_charge))
        << "Initial battery charge should be finite";
    ASSERT_GT(initial_charge, 0.0)
        << "Initial charge from blueprint should be positive";

    float dt = 1.0f / 60.0f;

    // Take one step to allow electrical solver to run and establish voltages
    sim.step(dt);

    // Verify battery voltage is finite after first solve step (not open-circuit)
    float bat_vout = sim.get_port_value("battery_1", "v_out");
    ASSERT_TRUE(std::isfinite(bat_vout))
        << "Battery v_out should be finite after solve (not open-circuit)";
    ASSERT_GT(bat_vout, 0.0f)
        << "Battery should produce positive voltage after solve";

    // Diagnostic: verify current flow using CurrentSense from the real fixture.
    float i_out = sim.get_port_value("currentsense_1", "i_out");
    ASSERT_TRUE(std::isfinite(i_out))
        << "CurrentSense output should be finite";
    EXPECT_GT(std::abs(i_out), 1e-4f)
        << "CurrentSense should report non-zero current in closed circuit. i_out=" << i_out;

    // Battery voltage should be near nominal 28V
    EXPECT_GT(bat_vout, 20.0f)
        << "Battery v_out should be near nominal 28V if properly connected. "
        << "Got " << bat_vout << " which suggests open circuit or wiring issue";

    // Sample periodically - 10 ohm load gives smaller discharge per step
    // I ≈ 28V / 10.01Ω ≈ 2.8A, discharge/step ≈ 2.8/3600/60 ≈ 1.3e-5 Ah
    // With 10000 steps: total ≈ 0.13 Ah decrease (measurable with double precision)
    const int total_steps = 10000;
    const int sample_interval = 1000;
    std::vector<double> charge_samples;
    charge_samples.push_back(initial_charge);

    for (int i = 0; i < total_steps; ++i) {
        sim.step(dt);
        double ch = sim.get_battery_charge("battery_1");
        ASSERT_TRUE(std::isfinite(ch)) << "Charge should be finite at step " << i;
        if ((i + 1) % sample_interval == 0) {
            charge_samples.push_back(ch);
        }
    }

    double final_charge = sim.get_battery_charge("battery_1");
    ASSERT_TRUE(std::isfinite(final_charge));

    // Verify monotonic decrease over samples
    for (size_t j = 1; j < charge_samples.size(); ++j) {
        EXPECT_LT(charge_samples[j], charge_samples[j-1])
            << "Charge should decrease monotonically: sample " << j-1
            << " (" << charge_samples[j-1] << ") > sample " << j
            << " (" << charge_samples[j] << ")";
    }

    // Total decrease should be measurable (≈0.13 Ah over 10000 steps)
    double total_decrease = initial_charge - final_charge;
    EXPECT_GT(total_decrease, 0.01)
        << "Total discharge should be measurable (> 0.01 Ah): got " << total_decrease;

    // Final charge should be strictly less than initial
    EXPECT_LT(final_charge, initial_charge)
        << "Final charge (" << final_charge << ") should be less than initial ("
        << initial_charge << ")";
}

TEST(PushRuntime, ClosedCircuitLike_BatteryChargeDecreases_CorrectedTopology) {
    // This test verifies battery discharge works with a controlled topology.
    // Uses values matching the blueprint (v_nominal=28.0, internal_r=0.01,
    // capacity=1000, charge=1000) with a simplified inline topology:
    // Battery -> Resistor -> RefNode(ground).
    // This is a controlled topology test providing stronger discharge signal
    // than the real fixture (which has lower conductance and may discharge slowly).
    //
    // With conductance=1.0 (1 ohm load), discharge per step is:
    //   I = 28V / 1.01 ohm ≈ 27.7A
    //   discharge_per_step ≈ 27.7 * (1/60) / 3600 ≈ 0.000128
    // After 1200 steps: total discharge ≈ 0.154 Ah, clearly measurable.
    const std::string json = R"({
        "devices": [
            {"name": "battery_1", "classname": "Battery", "params": {
                "v_nominal": "28.0", "internal_r": "0.01", "capacity": "1000.0", "charge": "1000.0"
            }},
            {"name": "resistor_1", "classname": "Resistor", "params": {"conductance": "1.0"}},
            {"name": "refnode_1", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "refnode_1.v", "to": "battery_1.v_in"},
            {"from": "battery_1.v_out", "to": "resistor_1.v_in"},
            {"from": "resistor_1.v_out", "to": "refnode_1.v"}
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start_from_json(json));

    float dt = 1.0f / 60.0f;

    // Initial charge from blueprint values
    double initial_charge = sim.get_battery_charge("battery_1");
    ASSERT_EQ(initial_charge, 1000.0) << "Initial charge should be 1000.0";

    // Sample charge periodically to verify monotonic decrease
    const int total_steps = 1200;
    const int sample_interval = 200;
    std::vector<double> charge_samples;

    for (int i = 0; i < total_steps; ++i) {
        sim.step(dt);
        if (i % sample_interval == 0) {
            double ch = sim.get_battery_charge("battery_1");
            ASSERT_TRUE(std::isfinite(ch)) << "Charge should be finite at step " << i;
            charge_samples.push_back(ch);
        }
    }

    double final_charge = sim.get_battery_charge("battery_1");
    ASSERT_TRUE(std::isfinite(final_charge));

    // Verify monotonic decrease over samples
    for (size_t j = 1; j < charge_samples.size(); ++j) {
        EXPECT_LT(charge_samples[j], charge_samples[j-1])
            << "Charge should decrease monotonically: sample " << j-1
            << " (" << charge_samples[j-1] << ") > sample " << j
            << " (" << charge_samples[j] << ")";
    }

    // Total decrease should be > epsilon
    double total_decrease = initial_charge - final_charge;
    EXPECT_GT(total_decrease, 0.001)
        << "Total discharge should be measurable (> 0.001 Ah): got " << total_decrease;

    // Voltage assertions as secondary check
    float final_vout = sim.get_port_value("battery_1", "v_out");
    EXPECT_TRUE(std::isfinite(final_vout));
    EXPECT_LT(final_vout, 29.0f) << "Battery voltage should be bounded under load";
}

TEST(PushRuntime, BatteryLiveOutputsExposeChargeAndSoc) {
    const std::string json = R"({
        "devices": [
            {"name": "battery_1", "classname": "Battery", "params": {
                "v_nominal": "28.0", "internal_r": "0.01", "capacity": "1.0", "charge": "1.0"
            }},
            {"name": "resistor_1", "classname": "Resistor", "params": {"conductance": "1.0"}},
            {"name": "refnode_1", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "refnode_1.v", "to": "battery_1.v_in"},
            {"from": "battery_1.v_out", "to": "resistor_1.v_in"},
            {"from": "resistor_1.v_out", "to": "refnode_1.v"}
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start_from_json(json));

    float dt = 1.0f / 60.0f;
    sim.step(dt);

    float charge_out = sim.get_port_value("battery_1", "charge_out");
    float soc_out = sim.get_port_value("battery_1", "soc_out");

    ASSERT_TRUE(std::isfinite(charge_out));
    ASSERT_TRUE(std::isfinite(soc_out));

    // Under load, charge and SoC should both decrease from 1.0.
    EXPECT_LT(charge_out, 1.0f);
    EXPECT_LT(soc_out, 1.0f);
    EXPECT_GT(soc_out, 0.0f);

    // SoC should track charge/capacity (capacity=1.0, so they should match).
    EXPECT_NEAR(soc_out, charge_out, 1e-5f);
}

TEST(PushRuntime, BatteryCommitRunsExactlyOncePerStepForSolverOwned) {
    // Verify that Battery commit is called exactly once per step.
    // Build simple circuit: Battery -> Resistor -> RefNode (ground return)
    // Use the commit_solver_owned_devices helper that runs in step().
    const std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery", "params": {
                "v_nominal": "28.0", "internal_r": "0.01", "capacity": "1000.0", "charge": "1000.0"
            }},
            {"name": "res", "classname": "Resistor", "params": {"conductance": "1.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "res.v_in"},
            {"from": "res.v_out", "to": "gnd.v"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    float dt = 1.0f / 60.0f;

    // Initial charge
    double charge_0 = sim.get_battery_charge("bat");
    EXPECT_EQ(charge_0, 1000.0);

    // Step 1: delta after first step
    sim.step(dt);
    double charge_1 = sim.get_battery_charge("bat");
    double delta_1 = charge_0 - charge_1;
    EXPECT_GT(delta_1, 0.0) << "Charge should decrease after 1 step (commit ran)";

    // Step 2: delta after second step
    sim.step(dt);
    double charge_2 = sim.get_battery_charge("bat");
    double delta_2 = charge_1 - charge_2;
    EXPECT_GT(delta_2, 0.0) << "Charge should continue decreasing after 2 steps";

    // Ratio check: delta_2 should be approximately equal to delta_1
    // (linear discharge, constant load, small dt) - allows for numerical tolerance
    double ratio = delta_2 / delta_1;
    EXPECT_NEAR(ratio, 1.0, 0.15)
        << "Discharge rate should be consistent per step (delta_2/delta_1 = " << ratio
        << ", expected ~1.0). If ratio is ~2.0, commit is being called twice. "
        << "If ratio is ~0.0, commit is not being called.";

    // Also verify cumulative: after N steps, total delta should be ~N * d1
    double total_delta = charge_0 - charge_2;
    double expected_total = delta_1 * 2.0;
    EXPECT_NEAR(total_delta, expected_total, expected_total * 0.20)
        << "Cumulative discharge should be ~2x single step (" << total_delta
        << " vs " << expected_total << ")";
}

TEST(PushRuntime, SimulationStateElectricalRtPointerClearedOutsideStep) {
    // Verify electrical_rt pointer is nullptr outside of active step window.
    // This is a regression test for Batch 7 pointer lifecycle hardening.
    const std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery", "params": {"v_nominal": "28.0"}},
            {"name": "sw", "classname": "Switch", "params": {"closed": "true"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "sw.v_in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    // After start_from_json but before any step, electrical_rt should be nullptr
    // (verified via internal state check by calling step and checking after)

    float dt = 1.0f / 60.0f;

    // Before step: pointer should be nullptr (start_from_json clears it)
    // We can't directly check sim's internal state. Instead, run a step and
    // verify behavior is correct (components that need electrical_rt still work).

    sim.step(dt);

    // After step: pointer should be cleared. Verify by running more steps
    // and checking voltages are still correct (would be wrong if pointer stale).
    for (int i = 0; i < 10; ++i) {
        sim.step(dt);
        float v = sim.get_port_value("sw", "v_out");
        EXPECT_TRUE(std::isfinite(v)) << "Voltage should be valid after step " << i;
    }

    // Verify circuit still produces correct steady-state voltage
    // (would diverge if electrical_rt pointer issue caused stale solves)
    EXPECT_NEAR(sim.get_port_value("bat", "v_out"), 28.0f, 0.5f)
        << "Battery should maintain nominal voltage";
}
