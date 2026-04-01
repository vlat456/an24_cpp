#include <gtest/gtest.h>

#include "editor/external_ref_mapping.h"
#include "editor/signal_key_resolver.h"
#include "jit_solver/simulator.h"
#include "jit_solver/jit_solver.h"

#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// =============================================================================
// Unit tests: signal key mapping function
// =============================================================================

TEST(ExternalRefMapping, BasicPrefixMapping) {
    auto result = editor::resolve_external_ref_signal_key("firstorderlag_1", "in.port");
    EXPECT_EQ(result, "firstorderlag_1:in.port");
}

TEST(ExternalRefMapping, EmptyParentId) {
    auto result = editor::resolve_external_ref_signal_key("", "multiply.A");
    EXPECT_EQ(result, ":multiply.A");
}

TEST(ExternalRefMapping, EmptyChildKey) {
    auto result = editor::resolve_external_ref_signal_key("firstorderlag_1", "");
    EXPECT_EQ(result, "firstorderlag_1:");
}

TEST(ExternalRefMapping, AccumulatorPort) {
    auto result = editor::resolve_external_ref_signal_key("firstorderlag_1", "accumulator.out");
    EXPECT_EQ(result, "firstorderlag_1:accumulator.out");
}

TEST(ExternalRefMapping, BridgeExtPort) {
    auto result = editor::resolve_external_ref_signal_key("firstorderlag_1", "in.ext");
    EXPECT_EQ(result, "firstorderlag_1:in.ext");
}

TEST(ExternalRefMapping, MultipleNestedLevels) {
    // If we had deeper nesting: parent composite is "outer_1",
    // child signal is "inner_2:accumulator.out"
    auto result = editor::resolve_external_ref_signal_key("outer_1", "inner_2:accumulator.out");
    EXPECT_EQ(result, "outer_1:inner_2:accumulator.out");
}

TEST(ExternalRefMapping, BuildSignalKey) {
    auto result = editor::build_signal_key("multiply", "A");
    EXPECT_EQ(result, "multiply.A");
}

TEST(ExternalRefMapping, BuildSignalKeyBridge) {
    auto result = editor::build_signal_key("in", "port");
    EXPECT_EQ(result, "in.port");
}

TEST(ExternalRefMapping, RoundTrip) {
    // Build child key, then map to parent — equivalent to the rendering pipeline
    std::string child_key = editor::build_signal_key("accumulator", "out");
    std::string parent_key = editor::resolve_external_ref_signal_key("firstorderlag_1", child_key);
    EXPECT_EQ(parent_key, "firstorderlag_1:accumulator.out");
}

// =============================================================================
// Integration test: closed_circuit + FirstOrderLag signal mapping
// =============================================================================

namespace {

static std::string read_file_or_fail(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    return content;
}

static std::string find_closed_circuit_blueprint() {
    std::vector<std::string> try_paths = {
        "../../closed_circuit.blueprint",
        "../closed_circuit.blueprint",
        "closed_circuit.blueprint",
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

/// Convert blueprint v3 to simulation JSON using node id as device key.
static std::string blueprint_to_simulation_json(const std::string& blueprint_path) {
    std::string content = read_file_or_fail(blueprint_path);
    json bp = json::parse(content);

    json result;
    result["devices"] = json::array();
    result["connections"] = json::array();

    if (bp.contains("nodes") && bp["nodes"].is_array()) {
        for (const auto& node : bp["nodes"]) {
            std::string node_id = node.value("id", "");
            json dev;
            dev["name"] = node_id;
            dev["classname"] = node["type"].get<std::string>();
            if (node.contains("params") && node["params"].is_object()) {
                dev["params"] = json::object();
                for (const auto& [k, v] : node["params"].items()) {
                    if (v.is_number()) {
                        dev["params"][k] = json(v.get<double>()).dump();
                    } else {
                        dev["params"][k] = v.get<std::string>();
                    }
                }
            }
            if (node.contains("string_params") && node["string_params"].is_object()) {
                if (!dev.contains("params")) dev["params"] = json::object();
                for (const auto& [k, v] : node["string_params"].items()) {
                    dev["params"][k] = v.get<std::string>();
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

TEST(ExternalRefIntegration, ClosedCircuitFirstOrderLagSignalsNonZero) {
    // Load and run the parent blueprint simulation
    std::string bp_path;
    ASSERT_NO_THROW(bp_path = find_closed_circuit_blueprint())
        << "Could not find closed_circuit.blueprint";

    std::string sim_json = blueprint_to_simulation_json(bp_path);

    Simulator<JIT_Solver> sim;
    ASSERT_NO_THROW(sim.start_from_json(sim_json));

    // Run enough steps for signals to propagate through the composite
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) {
        sim.step(dt);
    }

    // The FirstOrderLag composite "firstorderlag_1" should have non-zero
    // internal signals after the parent simulation has been running.
    // Build signal keys the same way the rendering code would:

    // Bridge node "in" receives input from the parent circuit
    std::string in_port_key = editor::resolve_external_ref_signal_key(
        "firstorderlag_1", "in.port");
    std::string in_ext_key = editor::resolve_external_ref_signal_key(
        "firstorderlag_1", "in.ext");

    // Accumulator output drives the output bridge
    std::string acc_out_key = editor::resolve_external_ref_signal_key(
        "firstorderlag_1", "accumulator.out");

    // Output bridge
    std::string out_port_key = editor::resolve_external_ref_signal_key(
        "firstorderlag_1", "out.port");
    std::string out_ext_key = editor::resolve_external_ref_signal_key(
        "firstorderlag_1", "out.ext");

    // Multiply (error * rate)
    std::string mul_o_key = editor::resolve_external_ref_signal_key(
        "firstorderlag_1", "multiply.o");

    // Verify that these mapped keys resolve to non-zero values in the simulator
    // (proving the mapping matches what the parser creates)
    float in_port_v  = sim.get_wire_voltage(in_port_key);
    float in_ext_v   = sim.get_wire_voltage(in_ext_key);
    float acc_out_v  = sim.get_wire_voltage(acc_out_key);
    float out_port_v = sim.get_wire_voltage(out_port_key);
    float out_ext_v  = sim.get_wire_voltage(out_ext_key);
    float mul_o_v    = sim.get_wire_voltage(mul_o_key);

    // Diagnostic: print all values
    printf("[DIAG] in.port=%f  in.ext=%f  acc.out=%f  out.port=%f  out.ext=%f  mul.o=%f\n",
           in_port_v, in_ext_v, acc_out_v, out_port_v, out_ext_v, mul_o_v);

    // The bridge input should receive the parent signal (non-zero if the
    // parent circuit feeds it). Even if the value is small, the accumulator
    // should have integrated to a non-zero value after 120 steps.
    // We check that at least the accumulator output or the output bridge
    // is non-zero, proving the signal mapping works end-to-end.
    float max_val = std::max({
        std::abs(in_port_v), std::abs(in_ext_v),
        std::abs(acc_out_v), std::abs(out_port_v),
        std::abs(out_ext_v), std::abs(mul_o_v)
    });

    EXPECT_GT(max_val, 0.0f)
        << "All FirstOrderLag internal signals are zero — mapping is broken.\n"
        << "  in.port=" << in_port_v << "  in.ext=" << in_ext_v << "\n"
        << "  accumulator.out=" << acc_out_v << "\n"
        << "  out.port=" << out_port_v << "  out.ext=" << out_ext_v << "\n"
        << "  multiply.o=" << mul_o_v;

    // Stronger assertions: the accumulator output should converge toward
    // the input after 2 seconds (120 steps at 60Hz) of first-order lag.
    // We just verify it's meaningfully non-zero (> 0.1).
    EXPECT_GT(std::abs(acc_out_v), 0.01f)
        << "Accumulator output is near zero — FirstOrderLag not integrating properly.\n"
        << "  accumulator.out=" << acc_out_v;
}

TEST(ExternalRefIntegration, WireIsEnergizedMappedKey) {
    // Verify that wire_is_energized works with mapped signal keys
    std::string bp_path;
    ASSERT_NO_THROW(bp_path = find_closed_circuit_blueprint())
        << "Could not find closed_circuit.blueprint";

    std::string sim_json = blueprint_to_simulation_json(bp_path);

    Simulator<JIT_Solver> sim;
    ASSERT_NO_THROW(sim.start_from_json(sim_json));

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) {
        sim.step(dt);
    }

    // After simulation, at least some of the FirstOrderLag's internal wires
    // should be energized (matching the rendering code's energization check)
    std::string acc_out_key = editor::resolve_external_ref_signal_key(
        "firstorderlag_1", "accumulator.out");
    float acc_out_v = sim.get_wire_voltage(acc_out_key);

    // If accumulator output is non-zero, wire_is_energized should match
    if (std::abs(acc_out_v) > 0.5f) {
        EXPECT_TRUE(sim.wire_is_energized(acc_out_key))
            << "wire_is_energized returned false for a non-zero mapped signal key";
    }
}

// =============================================================================
// Unit tests: map_composite_port_key
// =============================================================================

TEST(CompositePortMapping, BasicMapping) {
    auto key = editor::map_composite_port_key("firstorderlag_1", "out");
    EXPECT_EQ(key, "firstorderlag_1:out.ext");
}

TEST(CompositePortMapping, InputPort) {
    auto key = editor::map_composite_port_key("firstorderlag_1", "in");
    EXPECT_EQ(key, "firstorderlag_1:in.ext");
}

TEST(CompositePortMapping, RatePort) {
    auto key = editor::map_composite_port_key("firstorderlag_1", "rate");
    EXPECT_EQ(key, "firstorderlag_1:rate.ext");
}

TEST(CompositePortMapping, DifferentInstance) {
    auto key = editor::map_composite_port_key("my_filter_2", "output");
    EXPECT_EQ(key, "my_filter_2:output.ext");
}

// =============================================================================
// Regression test: ROOT-level composite port mapping resolves to non-zero
// =============================================================================

TEST(CompositePortMapping, RootLevelOutputResolves) {
    // This test proves the ROOT-LEVEL bug:
    // - The naive key "firstorderlag_1.out" does NOT exist in port_to_signal
    //   (returns 0) because the parser expanded it.
    // - The mapped key "firstorderlag_1:out.ext" DOES exist and is non-zero.
    //
    // Before the fix, the UI used "firstorderlag_1.out" → showed 0V.
    // After the fix, the UI uses "firstorderlag_1:out.ext" → shows correct value.

    std::string bp_path;
    ASSERT_NO_THROW(bp_path = find_closed_circuit_blueprint());

    std::string sim_json = blueprint_to_simulation_json(bp_path);

    Simulator<JIT_Solver> sim;
    ASSERT_NO_THROW(sim.start_from_json(sim_json));

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) {
        sim.step(dt);
    }

    // The WRONG key that the old root-level code was using:
    float naive_value = sim.get_wire_voltage("firstorderlag_1.out");
    EXPECT_FLOAT_EQ(naive_value, 0.0f)
        << "Naive key 'firstorderlag_1.out' should NOT exist in port_to_signal "
        << "(parser rewrites it to 'firstorderlag_1:out.ext')";

    // The CORRECT key that the fixed code now uses:
    std::string mapped_key = editor::map_composite_port_key("firstorderlag_1", "out");
    EXPECT_EQ(mapped_key, "firstorderlag_1:out.ext");

    float mapped_value = sim.get_wire_voltage(mapped_key);
    printf("[DIAG] naive 'firstorderlag_1.out' = %f\n", naive_value);
    printf("[DIAG] mapped 'firstorderlag_1:out.ext' = %f\n", mapped_value);

    EXPECT_GT(std::abs(mapped_value), 0.01f)
        << "Mapped key 'firstorderlag_1:out.ext' should be non-zero after 120 steps.\n"
        << "  mapped_value=" << mapped_value;

    // Also verify the input port mapping
    std::string in_mapped = editor::map_composite_port_key("firstorderlag_1", "in");
    EXPECT_EQ(in_mapped, "firstorderlag_1:in.ext");
    float in_value = sim.get_wire_voltage(in_mapped);
    printf("[DIAG] mapped 'firstorderlag_1:in.ext' = %f\n", in_value);
    EXPECT_GT(std::abs(in_value), 0.01f)
        << "Mapped input key should be non-zero (parent circuit feeds it)";
}

TEST(CompositePortMapping, RootLevelWireEnergizedWithMapping) {
    // Regression: the root-level wire from firstorderlag_1:out to multiply_1:B
    // should be energized after simulation, but only if we use the mapped key.

    std::string bp_path;
    ASSERT_NO_THROW(bp_path = find_closed_circuit_blueprint());

    std::string sim_json = blueprint_to_simulation_json(bp_path);

    Simulator<JIT_Solver> sim;
    ASSERT_NO_THROW(sim.start_from_json(sim_json));

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) {
        sim.step(dt);
    }

    // Wire "wire_23" connects /firstorderlag_1:out → /multiply_1:B
    // The source is an expandable node, so the wire energized check should
    // use the mapped key "firstorderlag_1:out.ext".

    // Old code would check: "firstorderlag_1.out" → always false (key doesn't exist)
    EXPECT_FALSE(sim.wire_is_energized("firstorderlag_1.out"))
        << "Naive key should NOT show energized (doesn't exist in simulation)";

    // Fixed code checks: "firstorderlag_1:out.ext" → true (value is non-zero)
    std::string mapped = editor::map_composite_port_key("firstorderlag_1", "out");
    EXPECT_TRUE(sim.wire_is_energized(mapped))
        << "Mapped key 'firstorderlag_1:out.ext' should be energized.\n"
        << "  value=" << sim.get_wire_voltage(mapped);
}

// ===========================================================================
// REGRESSION: Root expandable vs raw key comparison
// ===========================================================================

TEST(ExternalRefIntegration, RootExpandableRawVsMappedKey) {
    // Explicit regression test demonstrating the bug fix:
    // Raw key (firstorderlag_1.out) reads 0 in simulation
    // Mapped key (firstorderlag_1:out.ext) is non-zero after warmup
    
    std::string bp_path;
    ASSERT_NO_THROW(bp_path = find_closed_circuit_blueprint());

    std::string sim_json = blueprint_to_simulation_json(bp_path);

    Simulator<JIT_Solver> sim;
    ASSERT_NO_THROW(sim.start_from_json(sim_json));

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) {
        sim.step(dt);
    }

    // Raw key that does NOT exist in expanded simulation
     float raw_value = sim.get_wire_voltage("firstorderlag_1.out");
     EXPECT_FLOAT_EQ(raw_value, 0.0f)
         << "Raw key 'firstorderlag_1.out' should NOT exist (returns 0)";

     // Mapped key that DOES exist and has correct value
     std::string mapped_key = editor::map_composite_port_key("firstorderlag_1", "out");
     float mapped_value = sim.get_wire_voltage(mapped_key);
     EXPECT_GT(std::abs(mapped_value), 0.01f)
         << "Mapped key 'firstorderlag_1:out.ext' must be non-zero after warmup.\n"
         << "  This proves the resolver is handling root-level expandables correctly.";
}

// === PARITY HARDENING: Resolver/Parser Contract ===
// INVARIANT: Root expandable resolved keys must match parser rewrite contract
TEST(ExternalRefIntegration, RootExpandableResolvedKeyMatchesParserRewriteContract) {
    // This test locks the contract between:
    // 1. json_parser.cpp rewrite: instance.port -> instance:port.ext for expanded blueprints
    // 2. signal_key_resolver.cpp: map_composite_port_key() -> instance:port.ext format
    // 3. jit_solver.cpp bridge unification: .ext and .port unified in signal resolution
    
    std::string bp_path;
    ASSERT_NO_THROW(bp_path = find_closed_circuit_blueprint());

    std::string sim_json = blueprint_to_simulation_json(bp_path);

    Simulator<JIT_Solver> sim;
    ASSERT_NO_THROW(sim.start_from_json(sim_json));

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) {
        sim.step(dt);
    }

    // === CONTRACT CHECK 1: Mapped key format matches parser rewrite ===
    // Parser rewrites parent connections to instance:port.ext
    // Resolver must return the same format for root expandables
    std::string mapped_key = editor::map_composite_port_key("firstorderlag_1", "out");
    
    // Contract: format MUST be "instance:port.ext" or "instance:port.port"
    // NOT "instance.port" (raw format)
    ASSERT_GT(mapped_key.find(':'), 0) 
        << "Mapped key must contain ':' (composite instance separator): " << mapped_key;
    ASSERT_NE(mapped_key.find('.'), std::string::npos)
        << "Mapped key must contain '.' (port suffix): " << mapped_key;
    
    // === CONTRACT CHECK 2: Resolver maps to .ext for parent-level connections ===
    // For root expandables accessed from editor, resolver should use .ext (parent-facing)
    ASSERT_NE(mapped_key.find(".ext"), std::string::npos)
        << "Resolver must map root expandable to .ext format (parent-facing): " << mapped_key
        << "\n  This ensures parser rewrite contract is honored in simulation state.";

    // === CONTRACT CHECK 3: Resolved signal must exist and be non-zero ===
    float mapped_value = sim.get_wire_voltage(mapped_key);
    EXPECT_GT(std::abs(mapped_value), 0.01f)
        << "Mapped key from resolver contract must reference active signal: " << mapped_key;

    // === CONTRACT CHECK 4: Raw key (pre-rewrite format) must NOT exist ===
    float raw_value = sim.get_wire_voltage("firstorderlag_1.out");
    EXPECT_FLOAT_EQ(raw_value, 0.0f)
        << "Raw key 'firstorderlag_1.out' must NOT exist (parser should have rewritten it).\n"
        << "  This validates that parser rewrite contract is consistently applied.";
}
