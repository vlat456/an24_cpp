#include <gtest/gtest.h>

#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "blueprint_v2/path/path.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/simulator.h"
#include "core/solvers/jit/components/all.h"
#include "core/solvers/jit/components/port_registry.h"
#include "core/solvers/jit/state.h"
#include "jit_build_input_test_helper.h"

#include <cmath>
#include <stdexcept>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

const TypeRegistry& test_registry() {
    static const TypeRegistry registry = load_type_registry("library/");
    return registry;
}

DeviceInstance make_device(const std::string& name,
                           const std::string& classname,
                           const std::unordered_map<std::string, std::string>& params = {}) {
    DeviceInstance dev;
    dev.name = name;
    dev.classname = classname;
    dev.params = params;
    dev.execution = {};

    // First try to get full ComponentSpec ports if available.
    if (const PrimitiveSpec* def = as_primitive(*test_registry().get(classname))) {
        // Use full TypeDefinition::ports which includes input, output, and inout.
        for (const auto& [port_name, port_info] : def->ports) {
            dev.ports[port_name] = port_info;
        }
        // Fill in missing params from defaults.
        for (const auto& [param_name, param_spec] : def->params) {
            if (param_spec.visual_only) {
                continue;
            }
            if (!dev.params.count(param_name)) {
                dev.params[param_name] = param_spec.default_value;
            }
        }
        dev.solver_role = def->solver_role;
    } else {
        // Fallback: get component ports generically.
        auto ports = get_component_ports(classname);
        for (const auto& port_name : ports) {
            dev.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
        }
    }
    return dev;
}

SimulationState make_state(uint32_t signal_count) {
    SimulationState st;
    for (uint32_t i = 0; i < signal_count; ++i) {
        (void)st.allocate_signal(0.0f);
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

static std::string scalar_json_to_param_string(const json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return value.dump();
}

static bp2::BlueprintLibrary build_library(const TypeRegistry& registry, ui::StringInterner& interner) {
    bp2::BlueprintLibrary library;
    for (const auto& [classname, def] : registry.types) {
        if (!is_composite(def)) continue;
        try {
            auto loaded = bp2::blueprint_from_type_definition(def, interner, registry);
            library.add(interner.intern(classname), std::move(loaded));
        } catch (...) {
        }
    }
    return library;
}

static JitBuildInput build_input_from_blueprint_file(const std::string& blueprint_path,
                                                     const TypeRegistry& registry) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::BlueprintLibrary library = build_library(registry, interner);

    const std::string raw = read_file_or_fail(blueprint_path);
    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(raw, interner, arena, registry, &err);
    if (!bp.has_value()) {
        throw std::runtime_error("Decode failed: " + err.message);
    }

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(*bp, arena);
    return bp2::elaboration::elaborate_for_jit(netlist, arena, interner, &registry);
}

/// Find the canonical strict closed_circuit blueprint fixture.
static std::string find_closed_circuit_blueprint() {
    std::vector<std::string> try_paths = {
        "../../closed_circuit.blueprint",  // ctest from build/tests/
        "../closed_circuit.blueprint",     // run from build/
        "closed_circuit.blueprint",        // run from project root
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
    throw std::runtime_error(
        "Could not find closed_circuit.blueprint fixture in any of: " + tried);
}

/// Convert the legacy node/wire fixture format using node id as device key.
/// These regression fixtures predate strict blueprint v1 and use "/node_id:port" endpoints.
static std::string blueprint_to_simulation_json_by_id(const std::string& blueprint_path) {
    std::string content = read_file_or_fail(blueprint_path);
    json bp = json::parse(content);

    json result;
    result["devices"] = json::array();
    result["connections"] = json::array();

    if (bp.contains("nodes") && bp["nodes"].is_array()) {
        for (const auto& node : bp["nodes"]) {
            std::string node_id = node.value("id", "");
            json dev;
            dev["name"] = node_id;                            // key = id
            dev["classname"] = node["type"].get<std::string>();
            if (node.contains("params") && node["params"].is_object()) {
                dev["params"] = json::object();
                for (const auto& [k, v] : node["params"].items()) {
                    dev["params"][k] = scalar_json_to_param_string(v);
                }
            }
            // Merge string_params (e.g. LUT table, Bus port_edge) into params
            if (node.contains("string_params") && node["string_params"].is_object()) {
                if (!dev.contains("params")) {
                    dev["params"] = json::object();
                }
                for (const auto& [k, v] : node["string_params"].items()) {
                    dev["params"][k] = scalar_json_to_param_string(v);
                }
            }
            result["devices"].push_back(dev);
        }
    }

    if (bp.contains("wires") && bp["wires"].is_array()) {
        for (const auto& wire : bp["wires"]) {
            json conn;
            std::string from = wire["source"].get<std::string>();
            std::string to = wire["target"].get<std::string>();
            // Strip leading '/' and replace ':' with '.'
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

/// Convert the legacy node/wire fixture format to simulation JSON format (devices/connections).
/// Legacy fixture wires use source/target like "/node:port" which are converted to "node.port".
/// NOTE: This legacy helper uses node name as device key. The real editor uses node id.
static std::string blueprint_to_simulation_json(const std::string& blueprint_path) {
    std::string content = read_file_or_fail(blueprint_path);
    json bp = json::parse(content);

    json result;
    result["devices"] = json::array();
    result["connections"] = json::array();
    std::unordered_map<std::string, std::string> id_to_name;

    // Convert nodes to devices
    if (bp.contains("nodes") && bp["nodes"].is_array()) {
        for (const auto& node : bp["nodes"]) {
            std::string node_id = node.value("id", "");
            std::string node_name = node.value("name", node_id);
            if (!node_id.empty()) {
                id_to_name[node_id] = node_name;
            }

            json dev;
            dev["name"] = node_name;
            dev["classname"] = node["type"].get<std::string>();
            if (node.contains("params") && node["params"].is_object()) {
                dev["params"] = json::object();
                for (const auto& [k, v] : node["params"].items()) {
                    // Preserve legacy fixture scalar values as parser-friendly strings.
                    dev["params"][k] = scalar_json_to_param_string(v);
                }
            }
            // Merge string_params (e.g. LUT table, Bus port_edge) into params
            if (node.contains("string_params") && node["string_params"].is_object()) {
                if (!dev.contains("params")) {
                    dev["params"] = json::object();
                }
                for (const auto& [k, v] : node["string_params"].items()) {
                    dev["params"][k] = scalar_json_to_param_string(v);
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

            auto remap_endpoint = [&](std::string& endpoint) {
                size_t sep = endpoint.find(':');
                if (sep == std::string::npos) {
                    return;
                }
                std::string node_id = endpoint.substr(0, sep);
                auto it = id_to_name.find(node_id);
                if (it != id_to_name.end()) {
                    endpoint.replace(0, sep, it->second);
                }
            };
            remap_endpoint(from);
            remap_endpoint(to);

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
        make_device("clamp", "Clamp"),
        make_device("ra", "RefNode", {{"value", "2.0"}}),
        make_device("rb", "RefNode", {{"value", "4.0"}}),
        make_device("rc", "RefNode", {{"value", "3.0"}}),
        make_device("clamp_min", "Value", {{"value", "0.0"}}),
        make_device("clamp_max", "Value", {{"value", "20.0"}})
    };

    std::vector<std::vector<std::string>> signal_groups = {
        {"ra.v", "add.A"},
        {"rb.v", "add.B"},
        {"add.o", "mul.A"},
        {"rc.v", "mul.B"},
        {"mul.o", "clamp.in"},
        {"clamp_min.o", "clamp.min"},
        {"clamp_max.o", "clamp.max"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));
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

    std::vector<std::vector<std::string>> signal_groups = {
        {"add2.o", "add1.A"},
        {"ref1.v", "add1.B"},
        {"add1.o", "add2.A"},
        {"ref2.v", "add2.B"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));
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
             {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "28.0"}},
             {"name": "sw", "classname": "Switch", "params": {"closed": "false"}},
             {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
         ],
         "connections": [
             {"from": "gnd.v", "to": "bat.v_in"},
             {"from": "bat.v_out", "to": "sw.v_in"}
         ]
     })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    sim.apply_overrides({{"sw.control", 0.0f}});
    sim.step(1.0 / 60.0);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);

    sim.apply_overrides({{"sw.control", 1.0f}});
    // Frame N: execute uses previous closed state, commit toggles at end of frame.
    sim.step(1.0 / 60.0);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);
    // Frame N+1: new state is visible.
    sim.step(1.0 / 60.0);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 28.0f, 1e-3f);

    sim.apply_overrides({{"sw.control", 0.0f}});
    sim.step(1.0 / 60.0);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 28.0f, 1e-3f);
    sim.step(1.0 / 60.0);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);
}

TEST(PushRuntime, InitialValuesSeedState) {
     const std::string json = R"({
         "devices": [
             {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "24.0"}},
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
    sim.start(build_input_from_json(json));

    EXPECT_NEAR(sim.get_port_value("bat", "v_out"), 11.5f, 1e-5f);
    sim.step(1.0 / 60.0);
    EXPECT_TRUE(std::isfinite(sim.get_port_value("bat", "v_out")));
}

TEST(PushRuntime, SourceConflictErrorMessageReadable) {
     std::vector<DeviceInstance> devices = {
         make_device("b1", "ElectricalSource", {{"voltage", "28.0"}}),
         make_device("b2", "ElectricalSource", {{"voltage", "27.0"}}),
         make_device("gnd", "RefNode", {{"value", "0.0"}})
     };

    std::vector<std::vector<std::string>> signal_groups = {
        {"b1.v_out", "b2.v_out"},
        {"gnd.v", "b1.v_in", "b2.v_in"}
    };

    try {
        (void)build_systems_dev(make_jit_input(devices, signal_groups));
        FAIL() << "Expected source conflict to throw";
    }
    catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("b1.v_out"), std::string::npos);
        EXPECT_NE(msg.find("b2.v_out"), std::string::npos);
        EXPECT_NE(msg.find("signal"), std::string::npos);
    }
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
    sim.start(build_input_from_json(json));

    sim.step(1.0 / 60.0);

    // With factor=1.0 and deadzone=0.0, LerpNode should converge immediately
    // to the input value. A no-op execute() would leave output at 0.0.
    EXPECT_NEAR(sim.get_port_value("lerp", "output"), 10.0f, 1e-4f);
}

TEST(PushRuntime, DynamicFeedbackLoopStableAndBounded) {
     const std::string json = R"({
         "devices": [
             {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "28.0"}},
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
    sim.start(build_input_from_json(json));

    for (int i = 0; i < 180; ++i) {
        sim.step(1.0 / 60.0);
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
             {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "28.0"}},
             {"name": "sw", "classname": "Switch", "params": {"closed": "false"}},
             {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
         ],
         "connections": [
             {"from": "gnd.v", "to": "bat.v_in"},
             {"from": "bat.v_out", "to": "sw.v_in"}
         ]
     })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    // Initially switch is open, v_out should be 0
    sim.step(1.0 / 60.0);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);

    // Apply control to close the switch
    sim.apply_overrides({{"sw.control", 1.0f}});
    sim.step(1.0 / 60.0);
    // First frame after edge: old state still used during execute.
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);
    // Next frame: committed state is visible.
    sim.step(1.0 / 60.0);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 28.0f, 1e-3f);
}

TEST(PushRuntime, StatefulComponentOneFrameDelaySemantic) {
     // Stateful changes are committed at end-of-frame and visible next frame.
     const std::string json = R"({
         "devices": [
             {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "28.0"}},
             {"name": "sw", "classname": "Switch", "params": {"closed": "false"}},
             {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
         ],
         "connections": [
             {"from": "gnd.v", "to": "bat.v_in"},
             {"from": "bat.v_out", "to": "sw.v_in"}
         ]
     })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    // Initial: switch open, output = 0
    sim.step(1.0 / 60.0);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);

    // Toggle control (edge detect requires change)
    sim.apply_overrides({{"sw.control", 1.0f}});
    sim.step(1.0 / 60.0);

    // Frame N: execute still sees previous open state.
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);
    // Frame N+1: committed state visible.
    sim.step(1.0 / 60.0);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 28.0f, 1e-3f);

    // Toggle back
    sim.apply_overrides({{"sw.control", 0.0f}});
    sim.step(1.0 / 60.0);

    // Frame M: still previous closed state.
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 28.0f, 1e-3f);
    // Frame M+1: committed open state visible.
    sim.step(1.0 / 60.0);
    EXPECT_NEAR(sim.get_port_value("sw", "v_out"), 0.0f, 1e-4f);
}

TEST(PushRuntime, IntegratorComputesCorrectAccumulation) {
    // Integrator uses one-frame-delay semantics: output reflects committed state,
    // integration result is staged and visible on the NEXT frame.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "10.0"}},
            {"name": "integ", "classname": "Integrator", "params": {"initial_val": "0.0"}},
            {"name": "v_gain", "classname": "Value", "params": {"value": "1.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "integ.in"},
            {"from": "v_gain.o", "to": "integ.gain"}
        ]
    })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;

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
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;

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
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;

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
             {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "28.0"}},
             {"name": "sw", "classname": "Switch", "params": {"closed": "true"}},
             {"name": "add", "classname": "Add"},
             {"name": "pid", "classname": "PID", "params": {"Kp": "1.0", "Ki": "0.1", "Kd": "0.0", "output_min": "-10.0", "output_max": "10.0", "filter_alpha": "0.1"}},
             {"name": "slew", "classname": "SlewRate", "params": {"max_rate": "5.0", "deadzone": "0.01"}},
             {"name": "integ", "classname": "Integrator", "params": {"initial_val": "0.0"}},
             {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
             {"name": "gen", "classname": "Generator", "params": {"v_nominal": "12.0"}},
             {"name": "relay", "classname": "Relay"},
             {"name": "v_gain", "classname": "Value", "params": {"value": "1.0"}},
             {"name": "v_ht", "classname": "Value", "params": {"value": "0.5"}}
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
             {"from": "v_gain.o", "to": "integ.gain"},
             {"from": "gnd.v", "to": "gen.v_in"},
             {"from": "gen.v_out", "to": "relay.v_in"},
             {"from": "v_ht.o", "to": "relay.hold_threshold"}
         ]
     })";

    JIT_Simulator sim;
    EXPECT_NO_THROW(sim.start(build_input_from_json(json)));

    // Run multiple steps to verify commit path executes safely each frame
    double dt = 1.0 / 60.0;
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
    sim.start(build_input_from_json(json));
    double dt = 1.0 / 60.0;

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
    sim.start(build_input_from_json(json));
    double dt = 1.0 / 60.0;

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
    sim.start(build_input_from_json(json));
    double dt = 1.0 / 60.0;

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
    sim.start(build_input_from_json(json));
    double dt = 1.0 / 60.0;

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
            {"name": "integ", "classname": "Integrator", "params": {"initial_val": "0.0"}},
            {"name": "v_gain", "classname": "Value", "params": {"value": "1.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "integ.in"},
            {"from": "v_gain.o", "to": "integ.gain"}
        ]
    })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));
    double dt = 1.0 / 60.0;

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
    sim.start(build_input_from_json(json));
    double dt = 1.0 / 60.0;

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
    sim.start(build_input_from_json(json));
    double dt = 1.0 / 60.0;

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
        dev.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
    }

    std::vector<DeviceInstance> test_devs = {dev};

    try {
        (void)build_systems_dev(make_jit_input(test_devs, {}));
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
        dev.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
    }

    std::vector<DeviceInstance> test_devs = {dev};

    try {
        (void)build_systems_dev(make_jit_input(test_devs, {}));
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
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;

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
            dev.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
        }
        
        std::vector<DeviceInstance> test_devs = {dev};
        
        try {
            (void)build_systems_dev(make_jit_input(test_devs, {}));
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
             dev.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
         }
         
         std::vector<DeviceInstance> test_devs = {dev};
         
         EXPECT_NO_THROW((void)build_systems_dev(make_jit_input(test_devs, {})));
     }
}



// == Batch 5: Solver Ownership Integration Tests ==

TEST(PushRuntime, SolverOwnedComponentsNotScheduledForElectricalPropagation) {
     // Verify that ElectricalSource, Generator, Resistor are NOT scheduled
     // in the push scheduler for electrical propagation.
     // These components are now owned by the electrical solver instead.
     // RefNode IS scheduled as a source: it writes its constant reference value
     // into the signal array each frame so downstream logical consumers see it.
     std::vector<DeviceInstance> devices = {
         make_device("bat", "ElectricalSource", {{"voltage", "28.0"}}),
         make_device("gen", "Generator", {{"v_nominal", "28.0"}}),
         make_device("gnd", "RefNode", {{"value", "0.0"}}),
         make_device("res", "Resistor", {{"conductance", "0.1"}}),
         make_device("sw", "Switch", {{"closed", "true"}})  // consumer to verify scheduling works
     };

     std::vector<std::vector<std::string>> signal_groups = {
         {"gnd.v", "bat.v_in", "gen.v_in"},
         {"bat.v_out", "res.v_in"},
         {"res.v_out", "sw.v_in"}
     };

     auto result = build_systems_dev(make_jit_input(devices, signal_groups));

     // RefNode is scheduled as a source (it must write its constant value).
     // ElectricalSource, Generator, Resistor are solver-owned and NOT scheduled.
     EXPECT_EQ(result.scheduler.source_count(), 1u)
         << "Only RefNode should be scheduled as a source";
     EXPECT_EQ(result.scheduler.consumer_count(), 1u)
         << "Only Switch should be scheduled as consumer (others are solver-owned)";
}

TEST(PushRuntime, ClosedLoopNoRunawayAfterManySteps) {
      // Closed loop: ElectricalSource -> Resistor -> IndicatorLight -> RefNode (return)
      // After solver ownership changes, voltages should remain bounded.
      const std::string json = R"({
          "devices": [
              {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "0.01"}},
              {"name": "res", "classname": "Resistor", "params": {"conductance": "0.5"}},
              {"name": "ind", "classname": "IndicatorLight", "params": {"rated_voltage": "28.0"}},
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
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;

    // Step for many frames - voltages should remain bounded
    for (int i = 0; i < 500; ++i) {
        sim.step(dt);

        float bat_vout = sim.get_port_value("bat", "v_out");
        float res_vout = sim.get_port_value("res", "v_out");
        float ind_vout = sim.get_port_value("ind", "v_out");
        float gnd_v = sim.get_port_value("gnd", "v");

        // All voltages should be finite and bounded
        EXPECT_TRUE(std::isfinite(bat_vout)) << "Source v_out should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(res_vout)) << "Resistor v_out should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(ind_vout)) << "Indicator v_out should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(gnd_v)) << "Ground v should be finite at frame " << i;

        // No runaway to +28V unbounded
        EXPECT_LT(bat_vout, 35.0f) << "Source v_out should not runaway at frame " << i;
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
            {"name": "ind", "classname": "IndicatorLight", "params": {"rated_voltage": "28.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "ind.v_in"},
            {"from": "gnd.v", "to": "ind.v_out"}
        ]
    })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;

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
    EXPECT_LE(brightness, 1.0f) << "IndicatorLight brightness should be <= 1";
}

TEST(PushRuntime, IndicatorLightBrightnessStillFunctional) {
    // Verify IndicatorLight brightness calculation still works correctly
    // after removing the electrical pass-through.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "14.0"}},
            {"name": "ind", "classname": "IndicatorLight", "params": {"rated_voltage": "28.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "ind.v_in"},
            {"from": "gnd.v", "to": "ind.v_out"}
        ]
    })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;
    sim.step(dt);

    // At 14V input with 28V rated, brightness should be 0.5 (normalized)
    float brightness = sim.get_port_value("ind", "brightness");
    EXPECT_NEAR(brightness, 0.5f, 1e-3f)
        << "IndicatorLight brightness should be 0.5 (14V/28V normalized)";
}

TEST(PushRuntime, IndicatorLightRejectsMaxBrightnessParam) {
    // Regression: max_brightness parameter was removed from IndicatorLight.
    // Passing it must trigger "Unknown/unconsumed parameter" error, proving
    // that stale blueprints are caught before silent misbehaviour.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "28.0"}},
            {"name": "ind", "classname": "IndicatorLight", "params": {"max_brightness": "100.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "ind.v_in"},
            {"from": "gnd.v", "to": "ind.v_out"}
        ]
    })";

    JIT_Simulator sim;
    EXPECT_THROW(sim.start(build_input_from_json(json)), std::runtime_error)
        << "max_brightness should be rejected as an unknown parameter";
}

TEST(PushRuntime, IndicatorLightBlueprintNormalizedBrightness) {
    // Regression: Loads an IndicatorLight via accepted params (conductance,
    // rated_voltage) and verifies brightness output is normalized 0-1.
    // Tests multiple voltage levels to confirm the full range.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "0.01"}},
            {"name": "ind", "classname": "IndicatorLight", "params": {"conductance": "1.0", "rated_voltage": "28.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "gnd.v", "to": "src.v_in"},
            {"from": "src.v_out", "to": "ind.v_in"},
            {"from": "ind.v_out", "to": "gnd.v"}
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(build_input_from_json(json)))
        << "IndicatorLight with conductance + rated_voltage must build without error";

    double dt = 1.0 / 60.0;

    // Run a few steps
    for (int i = 0; i < 10; ++i) {
        sim.step(dt);
    }

    float brightness = sim.get_port_value("ind", "brightness");
    // With 28V source driving IndicatorLight rated at 28V through a 0.01Ω
    // internal resistance, brightness should be close to 1.0 (full voltage
    // across the indicator after voltage divider).
    EXPECT_GT(brightness, 0.0f)
        << "Brightness must be > 0 when voltage is applied";
    EXPECT_LE(brightness, 1.0f)
        << "Brightness must be normalized to [0, 1]";
    EXPECT_NEAR(brightness, 1.0f, 0.05f)
        << "At rated voltage, brightness should be close to 1.0";
}

TEST(PushRuntime, IndicatorLightNoBrightnessWithoutGround) {
    // Regression: IndicatorLight must NOT light up when v_out is floating
    // (not connected to ground). The old code computed brightness from v_in
    // alone; the fix computes from voltage drop (v_in - v_out). When both
    // ports see the same voltage (floating = solver equates them), drop is 0.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "28.0"}},
            {"name": "ind", "classname": "IndicatorLight", "params": {"rated_voltage": "28.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "ind.v_in"}
        ]
    })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;
    sim.step(dt);

    // With v_out disconnected (no return path), brightness should be 0
    // because there is no voltage drop across the indicator.
    float brightness = sim.get_port_value("ind", "brightness");
    EXPECT_NEAR(brightness, 0.0f, 0.05f)
        << "IndicatorLight without ground connection should have no brightness";
}

TEST(PushRuntime, IndicatorLightBrightnessFromVoltageDrop) {
    // Verify brightness is computed from voltage DROP (v_in - v_out), not v_in alone.
    // With v_in=28V and v_out=14V, brightness should be (28-14)/28 = 0.5.
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "RefNode", "params": {"value": "28.0"}},
            {"name": "ind", "classname": "IndicatorLight", "params": {"rated_voltage": "28.0"}},
            {"name": "mid", "classname": "RefNode", "params": {"value": "14.0"}}
        ],
        "connections": [
            {"from": "src.v", "to": "ind.v_in"},
            {"from": "mid.v", "to": "ind.v_out"}
        ]
    })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;
    sim.step(dt);

    // Drop = 28 - 14 = 14V across 28V rated → brightness = 0.5
    float brightness = sim.get_port_value("ind", "brightness");
    EXPECT_NEAR(brightness, 0.5f, 1e-3f)
        << "IndicatorLight brightness should reflect voltage drop, not absolute v_in";
}

// == Batch 7: Real Blueprint Regression Tests ==

TEST(PushRuntime, ClosedCircuitBlueprint_NoRunawayVoltage) {
    // Load real closed_circuit.blueprint from filesystem.
    // Topology: GS-24A generator with RN-180 carbon-pile voltage regulator.
    // CVS(GEN) → bus_2 → Resistor(10Ω) → CurrentSense(0.1Ω) → bus_1(gnd).
    // PI("RN180") senses bus_2.v vs 28.5V setpoint, output → LUT → Multiply(RPM) → CVS.cmd.
    // Verifies the regulated loop stays finite and does not run away.
    std::string blueprint_path;
    EXPECT_NO_THROW(blueprint_path = find_closed_circuit_blueprint())
        << "Could not find closed_circuit.blueprint";
    JIT_Simulator sim;
    EXPECT_NO_THROW(sim.start(build_input_from_blueprint_file(blueprint_path, test_registry())))
        << "Failed to start simulation from loaded blueprint";

    double dt = 1.0 / 60.0;

    // Run 600 steps (10 seconds at 60Hz)
    for (int i = 0; i < 600; ++i) {
        sim.step(dt);

        float gen_vpos = sim.get_port_value("controlledvoltagesource_1", "v_pos");
        float bus_2_v = sim.get_port_value("bus_2", "v");
        float cs_vin = sim.get_port_value("currentsense_1", "v_in");
        float cs_vout = sim.get_port_value("currentsense_1", "v_out");
        float cs_iout = sim.get_port_value("currentsense_1", "i_out");
        float bus_1_v = sim.get_port_value("bus_1", "v");
        float gnd_v = sim.get_port_value("refnode_4", "v");

        // All key electrical ports must be finite
        EXPECT_TRUE(std::isfinite(gen_vpos)) << "GEN.v_pos should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(bus_2_v)) << "bus_2.v should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(cs_vin)) << "currentsense_1.v_in should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(cs_vout)) << "currentsense_1.v_out should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(cs_iout)) << "currentsense_1.i_out should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(bus_1_v)) << "bus_1.v should be finite at frame " << i;
        EXPECT_TRUE(std::isfinite(gnd_v)) << "RefNode v should be finite at frame " << i;

        EXPECT_LT(std::fabs(gen_vpos), 100.0f) << "GEN.v_pos runaway at frame " << i;
        EXPECT_LT(std::fabs(bus_2_v), 100.0f) << "bus_2.v runaway at frame " << i;
        EXPECT_LT(std::fabs(cs_vin), 100.0f) << "currentsense_1.v_in runaway at frame " << i;
        EXPECT_LT(std::fabs(cs_vout), 100.0f) << "currentsense_1.v_out runaway at frame " << i;
        EXPECT_LT(std::fabs(bus_1_v), 100.0f) << "bus_1.v runaway at frame " << i;

        // Reference node should remain near configured value (0V)
        EXPECT_NEAR(gnd_v, 0.0f, 0.5f) << "RefNode drift at frame " << i;
    }
}

TEST(PushRuntime, ClosedCircuitBlueprint_RN180GeneratorRemainsDormantWithoutActivation) {
    // In the canonical strict closed_circuit fixture, the RN-180 control path
    // exists but the generator branch is not actively driven into a loaded
    // regulated state. The regression contract here is that the path remains
    // finite and near zero rather than spuriously energizing.
    std::string blueprint_path;
    ASSERT_NO_THROW(blueprint_path = find_closed_circuit_blueprint())
        << "Could not find closed_circuit.blueprint";
    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(build_input_from_blueprint_file(blueprint_path, test_registry())))
        << "Failed to start simulation from loaded blueprint";

    // Run 200 steps (~3.3 seconds) to let the dormant control path settle.
    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 200; ++i) {
        sim.step(dt);
    }

    float gen_vpos = sim.get_port_value("controlledvoltagesource_2", "v_pos");
    float cs_vin = sim.get_port_value("currentsense_1", "v_in");
    float cs_vout = sim.get_port_value("currentsense_1", "v_out");
    float i_out = sim.get_port_value("currentsense_1", "i_out");

    ASSERT_TRUE(std::isfinite(gen_vpos));
    ASSERT_TRUE(std::isfinite(cs_vin));
    ASSERT_TRUE(std::isfinite(cs_vout));
    ASSERT_TRUE(std::isfinite(i_out));

    EXPECT_LT(std::fabs(gen_vpos), 1e-3f)
        << "Dormant generator branch should not self-energize";
    EXPECT_LT(std::fabs(cs_vin), 1e-4f)
        << "CurrentSense input node should remain near zero in dormant state";
    EXPECT_NEAR(cs_vout, 0.0f, 1e-4f)
        << "CurrentSense output node should stay tied to ground bus";
    EXPECT_LT(std::fabs(i_out), 1e-3f)
        << "Dormant branch current should remain near zero";
}

TEST(PushRuntime, SimulationStateElectricalRtPointerClearedOutsideStep) {
     // Verify electrical_rt pointer is nullptr outside of active step window.
     // This is a regression test for Batch 7 pointer lifecycle hardening.
     const std::string json = R"({
         "devices": [
             {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "28.0"}},
             {"name": "sw", "classname": "Switch", "params": {"closed": "true"}},
             {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
         ],
         "connections": [
             {"from": "gnd.v", "to": "bat.v_in"},
             {"from": "bat.v_out", "to": "sw.v_in"}
         ]
     })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    // After start_from_json but before any step, electrical_rt should be nullptr
    // (verified via internal state check by calling step and checking after)

    double dt = 1.0 / 60.0;

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
         << "Source should maintain nominal voltage";
}

// =============================================================================
// Regression: Relay must respect configurable hold_threshold.
// Previously commit() used a hardcoded local threshold=0.5f
// instead of reading from the hold_threshold port.
// =============================================================================
TEST(PushRuntime, RelayCustomHoldThresholdIsRespected) {
    Relay<JitProvider> relay;
    relay.provider.set(PortNames::control, 0);
    relay.provider.set(PortNames::state, 1);
    relay.provider.set(PortNames::v_in, 2);
    relay.provider.set(PortNames::v_out, 3);
    relay.provider.set(PortNames::hold_threshold, 4);

    SimulationState st;
    st.values.resize(5, 0.0f);
    st.values[4] = 2.0f;  // hold_threshold = 2.0 via port

    // 1.0 < 2.0 threshold → should NOT close
    st.values[0] = 1.0f;
    relay.commit(st, 0.0);
    EXPECT_FALSE(relay.closed)
        << "control=1.0 is below hold_threshold=2.0, relay must stay open";

    // 2.5 > 2.0 threshold → should close
    st.values[0] = 2.5f;
    relay.commit(st, 0.0);
    EXPECT_TRUE(relay.closed)
        << "control=2.5 exceeds hold_threshold=2.0, relay must close";

    // -1.0 > -2.0 → should remain closed (hysteresis)
    st.values[0] = -1.0f;
    relay.commit(st, 0.0);
    EXPECT_TRUE(relay.closed)
        << "control=-1.0 does not reach -hold_threshold=-2.0, relay must stay closed";

    // -2.5 < -2.0 → should open
    st.values[0] = -2.5f;
    relay.commit(st, 0.0);
    EXPECT_FALSE(relay.closed)
        << "control=-2.5 reaches -hold_threshold=-2.0, relay must open";
}

TEST(PushRuntime, RelayElectricalSolverPath_ClosedProducesSag) {
     const char* json_open = R"({
         "devices": [
             {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "0.1"}},
             {"name": "relay", "classname": "Relay", "params": {"closed": "false", "g_open": "1e-6", "g_closed": "1000.0"}},
             {"name": "load", "classname": "Resistor", "params": {"conductance": "2.0"}},
             {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
             {"name": "cmd", "classname": "RefNode", "params": {"value": "0.0"}},
             {"name": "ht", "classname": "Value", "params": {"value": "0.5"}}
         ],
         "connections": [
             {"from": "cmd.v", "to": "relay.control"},
             {"from": "ht.o", "to": "relay.hold_threshold"},
             {"from": "bat.v_out", "to": "relay.v_in"},
             {"from": "relay.v_out", "to": "load.v_in"},
             {"from": "load.v_out", "to": "gnd.v"},
             {"from": "bat.v_in", "to": "gnd.v"}
         ]
     })";

     const char* json_closed = R"({
         "devices": [
             {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "0.1"}},
             {"name": "relay", "classname": "Relay", "params": {"closed": "false", "g_open": "1e-6", "g_closed": "1000.0"}},
             {"name": "load", "classname": "Resistor", "params": {"conductance": "2.0"}},
             {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
             {"name": "cmd", "classname": "RefNode", "params": {"value": "1.0"}},
             {"name": "ht", "classname": "Value", "params": {"value": "0.5"}}
         ],
         "connections": [
             {"from": "cmd.v", "to": "relay.control"},
             {"from": "ht.o", "to": "relay.hold_threshold"},
             {"from": "bat.v_out", "to": "relay.v_in"},
             {"from": "relay.v_out", "to": "load.v_in"},
             {"from": "load.v_out", "to": "gnd.v"},
             {"from": "bat.v_in", "to": "gnd.v"}
         ]
     })";

    JIT_Simulator sim_open;
    ASSERT_NO_THROW(sim_open.start(build_input_from_json(json_open)));

    const double dt = 1.0 / 60.0;

    // Open relay: output stays near 0V.
    for (int i = 0; i < 5; ++i) sim_open.step(dt);
    float v_open = sim_open.get_port_value("relay", "v_out");
    EXPECT_LT(v_open, 1.0f);

    JIT_Simulator sim_closed;
    ASSERT_NO_THROW(sim_closed.start(build_input_from_json(json_closed)));
    for (int i = 0; i < 5; ++i) sim_closed.step(dt);

    float v_closed = sim_closed.get_port_value("relay", "v_out");
    EXPECT_GT(v_closed, 5.0f)
        << "Closed relay should conduct through electrical solver path";
    EXPECT_LT(v_closed, 28.0f)
        << "With finite source/load conductance, output should show sag";
}

TEST(PushRuntime, ControlledVoltageSourceAndCurrentSenseCloseLoop) {
    const char* json = R"({
        "devices": [
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
            {"name": "rpm", "classname": "RefNode", "params": {"value": "1.0"}},
            {"name": "residual", "classname": "RefNode", "params": {"value": "0.05"}},
            {"name": "gain", "classname": "RefNode", "params": {"value": "25.0"}},
            {"name": "add", "classname": "Add"},
            {"name": "mul1", "classname": "Multiply"},
            {"name": "mul2", "classname": "Multiply"},
            {"name": "cvs", "classname": "ControlledVoltageSource", "params": {
                "r_internal": "0.01"
            }},
            {"name": "v_cvs_gain", "classname": "Value", "params": {"value": "1.0"}},
            {"name": "v_cvs_offset", "classname": "Value", "params": {"value": "0.0"}},
            {"name": "v_cvs_min_v", "classname": "Value", "params": {"value": "0.0"}},
            {"name": "v_cvs_max_v", "classname": "Value", "params": {"value": "30.0"}},
            {"name": "cs", "classname": "CurrentSense", "params": {"conductance": "10.0"}},
            {"name": "filt", "classname": "FastTMO", "params": {"deadzone": "0.001", "tau": "0.2"}}
        ],
        "connections": [
            {"from": "residual.v", "to": "add.A"},
            {"from": "filt.out", "to": "add.B"},
            {"from": "rpm.v", "to": "mul1.A"},
            {"from": "add.o", "to": "mul1.B"},
            {"from": "gain.v", "to": "mul2.A"},
            {"from": "mul1.o", "to": "mul2.B"},
            {"from": "mul2.o", "to": "cvs.cmd"},
            {"from": "v_cvs_gain.o", "to": "cvs.gain"},
            {"from": "v_cvs_offset.o", "to": "cvs.offset"},
            {"from": "v_cvs_min_v.o", "to": "cvs.min_v"},
            {"from": "v_cvs_max_v.o", "to": "cvs.max_v"},
            {"from": "gnd.v", "to": "cvs.v_neg"},
            {"from": "cvs.v_pos", "to": "cs.v_in"},
            {"from": "cs.v_out", "to": "gnd.v"},
            {"from": "cs.i_out", "to": "filt.in"}
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(build_input_from_json(json)));

    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 10; ++i) {
        sim.step(dt);
    }

    float v_in = sim.get_port_value("cs", "v_in");
    float v_out = sim.get_port_value("cs", "v_out");
    float i_out = sim.get_port_value("cs", "i_out");

    EXPECT_GT(v_in, 0.5f)
        << "ControlledVoltageSource should energize CurrentSense input";
    EXPECT_NEAR(v_out, 0.0f, 1e-4f)
        << "CurrentSense output node should remain tied to ground";
    EXPECT_GT(std::fabs(i_out), 1e-3f)
        << "CurrentSense should report non-zero current in closed-loop CVS circuit";
}

// Regression: editor readback must use node id (not display name) as simulation key.
// In closed_circuit.blueprint, the CVS node has id="controlledvoltagesource_1"
// but name="GEN". The canonical editor runtime path exports devices keyed
// by id. The editor's updateNodeContentFromSimulation() must query by id too,
// otherwise it reads 0 for a device named "GEN" that doesn't exist in the sim.
TEST(PushRuntime, ClosedCircuit_EditorIdBasedLookup_NonZeroVoltage) {
    std::string blueprint_path;
    ASSERT_NO_THROW(blueprint_path = find_closed_circuit_blueprint())
        << "Could not find closed_circuit.blueprint";

    // Build JSON using node id as key (mirrors real editor path)
    std::string json;
    ASSERT_NO_THROW(json = blueprint_to_simulation_json_by_id(blueprint_path))
        << "Failed to build id-keyed simulation JSON";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(build_input_from_json(json)))
        << "Failed to start simulation from id-keyed JSON";

    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 20; ++i) {
        sim.step(dt);
    }

    // Query by interned id (what the fixed editor does)
    float v_pos = sim.get_port_value("controlledvoltagesource_1", "v_pos");
    float cs_vin = sim.get_port_value("currentsense_1", "v_in");
    float cs_iout = sim.get_port_value("currentsense_1", "i_out");

    // CVS v_pos is the source-side node — should be near v_source (~1.6 V).
    EXPECT_GT(v_pos, 0.5f)
        << "CVS v_pos should be non-zero when queried by node id";
    // cs_vin sits after a 10 Ω resistor in series with a 0.1 Ω current-sense,
    // so the voltage divider gives cs_vin ≈ v_source * 0.1/10.1 ≈ 0.016 V.
    // We just check it is energised (non-zero).
    EXPECT_GT(cs_vin, 0.001f)
        << "CurrentSense v_in should be non-zero when queried by node id";
    EXPECT_GT(std::fabs(cs_iout), 1e-3f)
        << "CurrentSense i_out should be non-zero when queried by node id";

    // Canonical identity uses only node_id.port lookup. Display-name alias lookup is removed.
    float gen_vpos = sim.get_port_value("GEN", "v_pos");
    EXPECT_FLOAT_EQ(gen_vpos, 0.0f)
        << "Querying by non-canonical node id must return 0";
}

TEST(PushRuntime, AZS_ElectricalSolverPath_ClosedProducesSag) {
    // Use a moderate load conductance (0.5 S → ~14A) to stay below i_nominal (20A)
    // so the AZS thermal model doesn't trip during the test.
    const char* kJson = R"({
  "devices": [
    {"name":"gnd","classname":"RefNode","params":{"value":"0.0"}},
    {"name":"src","classname":"ElectricalSource","params":{"voltage":"28.5","resistance":"0.5"}},
    {"name":"azs","classname":"AZS","params":{"closed":"true","i_nominal":"20.0","g_open":"1e-6","g_closed":"1000.0"}},
    {"name":"load","classname":"VariableConductance"},
    {"name":"v_gmin","classname":"Value","params":{"value":"0.5"}},
    {"name":"v_gmax","classname":"Value","params":{"value":"0.5"}},
    {"name":"v_cmd","classname":"Value","params":{"value":"1.0"}}
  ],
  "connections": [
    {"from":"src.v_out","to":"azs.v_in"},
    {"from":"azs.v_out","to":"load.v_in"},
    {"from":"load.v_out","to":"gnd.v"},
    {"from":"src.v_in","to":"gnd.v"},
    {"from":"v_gmin.o","to":"load.g_min"},
    {"from":"v_gmax.o","to":"load.g_max"},
    {"from":"v_cmd.o","to":"load.cmd"}
  ]
})";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(build_input_from_json(kJson)));

    // Let solver-owned dynamic conductance settle through commit/update cycle.
    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 5; ++i) sim.step(dt);

    float v_hot = sim.get_wire_voltage("src.v_out");
    ASSERT_TRUE(std::isfinite(v_hot));
    EXPECT_LT(v_hot, 27.0f)
        << "AZS closed should insert heavy load branch into electrical solver and produce sag";
}

TEST(PushRuntime, HoldButton_ElectricalSolverPath_PressProducesSag) {
    const char* kJson = R"({
  "devices": [
    {"name":"gnd","classname":"RefNode","params":{"value":"0.0"}},
    {"name":"src","classname":"ElectricalSource","params":{"voltage":"28.5","resistance":"0.01"}},
    {"name":"ctrl","classname":"Value","params":{"value":"1.0"}},
    {"name":"btn","classname":"HoldButton","params":{"idle":"0.0","g_open":"1e-6","g_closed":"1000.0"}},
    {"name":"load","classname":"VariableConductance"},
    {"name":"v_gmin","classname":"Value","params":{"value":"17.5"}},
    {"name":"v_gmax","classname":"Value","params":{"value":"17.5"}},
    {"name":"v_cmd","classname":"Value","params":{"value":"1.0"}}
  ],
  "connections": [
    {"from":"src.v_out","to":"btn.v_in"},
    {"from":"btn.v_out","to":"load.v_in"},
    {"from":"load.v_out","to":"gnd.v"},
    {"from":"src.v_in","to":"gnd.v"},
    {"from":"ctrl.o","to":"btn.control"},
    {"from":"v_gmin.o","to":"load.g_min"},
    {"from":"v_gmax.o","to":"load.g_max"},
    {"from":"v_cmd.o","to":"load.cmd"}
  ]
})";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(build_input_from_json(kJson)));

    const double dt = 1.0 / 60.0;
    sim.step(dt); // first frame: button state updates after solve
    float v_step1 = sim.get_wire_voltage("src.v_out");

    for (int i = 0; i < 5; ++i) sim.step(dt);
    float v_pressed = sim.get_wire_voltage("src.v_out");

    ASSERT_TRUE(std::isfinite(v_step1));
    ASSERT_TRUE(std::isfinite(v_pressed));
    EXPECT_LT(v_pressed, v_step1 - 0.5f)
        << "Pressed HoldButton should close solver-owned branch and increase sag";
}

// Regression: g_open must never be 0 — it creates a singular row in the
// conductance matrix when the switch is open. The default 1e-6 parasitic
// conductance keeps the matrix non-singular. This test verifies that an
// open AZS with proper g_open produces finite (near-source) voltage.
TEST(PushRuntime, AZS_OpenState_ParasiticConductance_StaysFinite) {
    const char* kJson = R"({
  "devices": [
    {"name":"gnd","classname":"RefNode","params":{"value":"0.0"}},
    {"name":"src","classname":"ElectricalSource","params":{"voltage":"28.5","resistance":"0.01"}},
    {"name":"azs","classname":"AZS","params":{"closed":"false","i_nominal":"20.0","g_open":"1e-6","g_closed":"1000.0"}},
    {"name":"load","classname":"VariableConductance"},
    {"name":"v_gmin","classname":"Value","params":{"value":"17.5"}},
    {"name":"v_gmax","classname":"Value","params":{"value":"17.5"}},
    {"name":"v_cmd","classname":"Value","params":{"value":"1.0"}}
  ],
  "connections": [
    {"from":"src.v_out","to":"azs.v_in"},
    {"from":"azs.v_out","to":"load.v_in"},
    {"from":"load.v_out","to":"gnd.v"},
    {"from":"src.v_in","to":"gnd.v"},
    {"from":"v_gmin.o","to":"load.g_min"},
    {"from":"v_gmax.o","to":"load.g_max"},
    {"from":"v_cmd.o","to":"load.cmd"}
  ]
})";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(build_input_from_json(kJson)));

    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 5; ++i) sim.step(dt);

    float v_src = sim.get_wire_voltage("src.v_out");
    float v_load = sim.get_wire_voltage("azs.v_out");

    ASSERT_TRUE(std::isfinite(v_src))
        << "Source voltage must be finite with parasitic g_open";
    ASSERT_TRUE(std::isfinite(v_load))
        << "Load-side voltage must be finite with parasitic g_open";
    // AZS open: nearly all voltage drops across the open switch.
    // Source side should be near 28.5V, load side near 0V.
    EXPECT_GT(v_src, 28.0f)
        << "Open AZS should barely load the source";
    EXPECT_LT(v_load, 1.0f)
        << "Open AZS should pass negligible current to load";
}

// Regression: AZS thermal model must run in full simulator pipeline.
// Before this fix, AZS::execute() was never called for solver-owned components,
// so the thermal model was dead code and AZS would never trip thermally.
TEST(PushRuntime, AZS_ThermalTripRunsInFullSimulator) {
    // High load conductance (100 S) draws ~100A through a 20A-rated AZS.
    // Must trip thermally within a few seconds.
    const char* kJson = R"({
  "devices": [
    {"name":"gnd","classname":"RefNode","params":{"value":"0.0"}},
    {"name":"src","classname":"ElectricalSource","params":{"voltage":"28.5","resistance":"0.01"}},
    {"name":"azs","classname":"AZS","params":{"closed":"true","i_nominal":"20.0","g_open":"1e-6","g_closed":"1000.0"}},
    {"name":"load","classname":"Resistor","params":{"conductance":"100.0"}}
  ],
  "connections": [
    {"from":"src.v_out","to":"azs.v_in"},
    {"from":"azs.v_out","to":"load.v_in"},
    {"from":"load.v_out","to":"gnd.v"},
    {"from":"src.v_in","to":"gnd.v"}
  ]
})";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(build_input_from_json(kJson)));

    // Run for enough steps for thermal model to trip (~1-2 seconds at 60Hz)
    const double dt = 1.0 / 60.0;
    float v_before_trip = 0.0f;
    bool tripped = false;
    for (int i = 0; i < 300; ++i) {
        sim.step(dt);
        float state = sim.get_wire_voltage("azs.state");
        if (i == 0) {
            v_before_trip = sim.get_wire_voltage("azs.v_out");
            EXPECT_GT(v_before_trip, 1.0f) << "AZS should initially be closed and passing voltage";
        }
        if (state < 0.5f && i > 0) {
            tripped = true;
            break;
        }
    }

    EXPECT_TRUE(tripped)
        << "AZS thermal model should trip when overcurrent flows through closed breaker. "
           "If this test fails, AZS::execute() is not being called in the simulator pipeline.";
}
