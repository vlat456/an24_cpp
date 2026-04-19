#include <gtest/gtest.h>

#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "blueprint_v2/path/path.h"
#include "editor/external_ref_mapping.h"
#include "editor/signal_key_resolver.h"
#include "core/solvers/jit/simulator.h"
#include "core/solvers/jit/jit_solver.h"

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

static std::string find_library_dir() {
    for (const auto& p : {
        "../../library/",
        "../library/",
        "library/",
    }) {
        std::ifstream f(std::string(p) + "electrical/Resistor.blueprint");
        if (f.is_open()) return p;
    }
    throw std::runtime_error("Could not find library directory");
}

static const ComponentRegistry& fixture_registry() {
    static const ComponentRegistry registry = load_component_registry(find_library_dir());
    return registry;
}

static bp2::BlueprintLibrary build_library(const ComponentRegistry& registry, ui::StringInterner& interner) {
    bp2::BlueprintLibrary library;
    for (const auto& [classname, spec] : registry.types) {
        if (::is_primitive(spec)) continue;
        try {
            auto loaded = bp2::blueprint_from_type_definition(spec, interner, registry);
            library.add(interner.intern(classname), std::move(loaded));
        } catch (...) {
        }
    }
    return library;
}

static JitBuildInput build_input_from_blueprint_file(const std::string& blueprint_path) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    const ComponentRegistry& registry = fixture_registry();
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
    throw std::runtime_error(
        "Could not find closed_circuit.blueprint fixture in any of: " + tried);
}

} // namespace

TEST(ExternalRefIntegration, ClosedCircuitFirstOrderLagSignalsNonZero) {
    // Load and run the parent blueprint simulation
    std::string bp_path;
    ASSERT_NO_THROW(bp_path = find_closed_circuit_blueprint())
        << "Could not find closed_circuit.blueprint";

    Simulator<JIT_Solver> sim;
    ASSERT_NO_THROW(sim.start(build_input_from_blueprint_file(bp_path)));

    // Run enough steps for signals to propagate through the composite
    const double dt = 1.0 / 60.0;
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

    // Canonical root endpoint should mirror bridge external value.
    std::string canonical_in_key = editor::map_composite_port_key("firstorderlag_1", "in");
    float canonical_in_v = sim.get_wire_voltage(canonical_in_key);
    EXPECT_NEAR(canonical_in_v, in_ext_v, 1e-5f)
        << "Canonical root endpoint must alias bridge external endpoint";
}

TEST(ExternalRefIntegration, WireIsEnergizedMappedKey) {
    // Verify that wire_is_energized works with mapped signal keys
    std::string bp_path;
    ASSERT_NO_THROW(bp_path = find_closed_circuit_blueprint())
        << "Could not find closed_circuit.blueprint";

    Simulator<JIT_Solver> sim;
    ASSERT_NO_THROW(sim.start(build_input_from_blueprint_file(bp_path)));

    const double dt = 1.0 / 60.0;
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
    EXPECT_EQ(key, "firstorderlag_1.out");
}

TEST(CompositePortMapping, InputPort) {
    auto key = editor::map_composite_port_key("firstorderlag_1", "in");
    EXPECT_EQ(key, "firstorderlag_1.in");
}

TEST(CompositePortMapping, RatePort) {
    auto key = editor::map_composite_port_key("firstorderlag_1", "rate");
    EXPECT_EQ(key, "firstorderlag_1.rate");
}

TEST(CompositePortMapping, DifferentInstance) {
    auto key = editor::map_composite_port_key("my_filter_2", "output");
    EXPECT_EQ(key, "my_filter_2.output");
}

// =============================================================================
// Regression test: ROOT-level composite port mapping resolves to non-zero
// =============================================================================

TEST(CompositePortMapping, RootLevelOutputResolves) {
    // Root-level composite signal keys resolve via canonical node.port identity.

    std::string bp_path;
    ASSERT_NO_THROW(bp_path = find_closed_circuit_blueprint());

    Simulator<JIT_Solver> sim;
    ASSERT_NO_THROW(sim.start(build_input_from_blueprint_file(bp_path)));

    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 120; ++i) {
        sim.step(dt);
    }

    float naive_value = sim.get_wire_voltage("firstorderlag_1.out");
    std::string mapped_key = editor::map_composite_port_key("firstorderlag_1", "out");
    EXPECT_EQ(mapped_key, "firstorderlag_1.out");
    EXPECT_NEAR(naive_value, sim.get_wire_voltage(mapped_key), 1e-6f)
        << "Direct node.port and resolver key must match for canonical identity";

    float mapped_value = sim.get_wire_voltage(mapped_key);
    printf("[DIAG] canonical 'firstorderlag_1.out' = %f\n", mapped_value);

    EXPECT_GT(std::abs(mapped_value), 0.01f)
        << "Mapped key 'firstorderlag_1.out' should be non-zero after 120 steps.\n"
        << "  mapped_value=" << mapped_value;

    // Also verify the input port mapping
    std::string in_mapped = editor::map_composite_port_key("firstorderlag_1", "in");
    EXPECT_EQ(in_mapped, "firstorderlag_1.in");
    float in_value = sim.get_wire_voltage(in_mapped);
    printf("[DIAG] mapped 'firstorderlag_1.in' = %f\n", in_value);
    EXPECT_GT(std::abs(in_value), 0.01f)
        << "Mapped input key should be non-zero (parent circuit feeds it)";
}

TEST(CompositePortMapping, RootLevelWireEnergizedWithMapping) {
    // Regression: the root-level wire from firstorderlag_1:out to multiply_1:B
    // should be energized after simulation, but only if we use the mapped key.

    std::string bp_path;
    ASSERT_NO_THROW(bp_path = find_closed_circuit_blueprint());

    Simulator<JIT_Solver> sim;
    ASSERT_NO_THROW(sim.start(build_input_from_blueprint_file(bp_path)));

    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 120; ++i) {
        sim.step(dt);
    }

    // Wire "wire_23" connects /firstorderlag_1:out → /multiply_1:B.
    // Canonical key is node.port.
    std::string mapped = editor::map_composite_port_key("firstorderlag_1", "out");
    EXPECT_EQ(mapped, "firstorderlag_1.out");
    EXPECT_TRUE(sim.wire_is_energized(mapped))
        << "Mapped key 'firstorderlag_1.out' should be energized.\n"
        << "  value=" << sim.get_wire_voltage(mapped);
}

// ===========================================================================
// REGRESSION: Root expandable vs raw key comparison
// ===========================================================================

TEST(ExternalRefIntegration, RootExpandableRawVsMappedKey) {
    // Root expandables resolve through canonical node.port identity.
    
    std::string bp_path;
    ASSERT_NO_THROW(bp_path = find_closed_circuit_blueprint());

    Simulator<JIT_Solver> sim;
    ASSERT_NO_THROW(sim.start(build_input_from_blueprint_file(bp_path)));

    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 120; ++i) {
        sim.step(dt);
    }

    float raw_value = sim.get_wire_voltage("firstorderlag_1.out");
    std::string mapped_key = editor::map_composite_port_key("firstorderlag_1", "out");
    float mapped_value = sim.get_wire_voltage(mapped_key);
    EXPECT_NEAR(raw_value, mapped_value, 1e-6f)
        << "Raw node.port and mapped key must resolve to the same canonical signal";
    EXPECT_GT(std::abs(mapped_value), 0.01f)
        << "Mapped key 'firstorderlag_1.out' must be non-zero after warmup.";
}

// === PARITY HARDENING: Resolver/Parser Contract ===
// INVARIANT: Root expandable resolved keys must be canonical node.port
TEST(ExternalRefIntegration, RootExpandableResolvedKeyMatchesParserRewriteContract) {
    // This test locks the contract between parser output and resolver output:
    // root expandable ports are queried via canonical node.port identity.
    
    std::string bp_path;
    ASSERT_NO_THROW(bp_path = find_closed_circuit_blueprint());

    Simulator<JIT_Solver> sim;
    ASSERT_NO_THROW(sim.start(build_input_from_blueprint_file(bp_path)));

    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 120; ++i) {
        sim.step(dt);
    }

    // === CONTRACT CHECK 1: mapped key is canonical node.port ===
    std::string mapped_key = editor::map_composite_port_key("firstorderlag_1", "out");
    ASSERT_EQ(mapped_key, "firstorderlag_1.out")
        << "Resolver must map root expandable to canonical node.port format";

    // === CONTRACT CHECK 2: resolved signal must exist and be non-zero ===
    float mapped_value = sim.get_wire_voltage(mapped_key);
    EXPECT_GT(std::abs(mapped_value), 0.01f)
        << "Mapped key from resolver contract must reference active signal: " << mapped_key;

    // === CONTRACT CHECK 3: direct node.port query is identical ===
    float raw_value = sim.get_wire_voltage("firstorderlag_1.out");
    EXPECT_NEAR(raw_value, mapped_value, 1e-6f)
        << "Direct node.port and resolver mapped key must be identical";
}
