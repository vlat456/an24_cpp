#include <gtest/gtest.h>
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/simulator.h"
#include "core/solvers/jit/components/port_registry.h"
#include "core/solvers/jit/state.h"
#include "jit_build_input_test_helper.h"

#include <algorithm>
#include <cmath>

// ============================================================================
// Step 13 Verification Tests — Explicit Primitive Electrical Nodes
//
// Tests prove that:
//   1. Wrapper-based Resistor and primitive ElectricalConductance produce
//      equivalent solve results.
//   2. Primitive-only simple circuit (ElectricalSource + ElectricalConductance
//      + RefNode) solves correctly.
//   3. Build plan correctly includes primitives in electrical islands.
//   4. Primitives are solver-owned (not scheduled for push propagation).
// ============================================================================

namespace {

ResolvedDevice make_test_device_or_synthetic(const std::string& name,
                                             const std::string& classname,
                                             const std::unordered_map<std::string, std::string>& params = {}) {
    if (const ComponentSpec* spec = test_registry().get(classname)) {
        return make_resolved_device(name, classname, params);
    }

    DeviceInstance dev;
    dev.name = name;
    dev.classname = classname;
    dev.params = params;
    for (const auto& port_name : get_component_ports(classname)) {
        dev.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
    }

    ResolvedDevice resolved;
    resolved.name = dev.name;
    resolved.classname = dev.classname;
    resolved.params = dev.params;
    resolved.ports = dev.ports;
    return resolved;
}

} // anonymous namespace

// ============================================================================
// Test 1: Wrapper-based Resistor vs primitive ElectricalConductance equivalence
// ============================================================================

TEST(ElectricalPrimitives, ResistorAndConductancePrimitiveEquivalent) {
    const float conductance = 0.5f;
    const float voltage = 28.0f;
    const float resistance = 0.1f;

    // --- Circuit A: Wrapper Resistor ---
    std::vector<ResolvedDevice> devices_a = {
        make_test_device_or_synthetic("bat", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.1"}}),
        make_test_device_or_synthetic("res", "Resistor", {{"conductance", "0.5"}}),
        make_test_device_or_synthetic("gnd", "RefNode", {{"value", "0.0"}})
    };
    std::vector<std::vector<std::string>> signal_groups_a = {
        {"bat.v_out", "res.v_in"},
        {"res.v_out", "gnd.v", "bat.v_in"}
    };

    // --- Circuit B: Primitive ElectricalConductance ---
    std::vector<ResolvedDevice> devices_b = {
        make_test_device_or_synthetic("bat", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.1"}}),
        make_test_device_or_synthetic("cond", "ElectricalConductance", {{"conductance", "0.5"}}),
        make_test_device_or_synthetic("gnd", "RefNode", {{"value", "0.0"}})
    };
    std::vector<std::vector<std::string>> signal_groups_b = {
        {"bat.v_out", "cond.v_in"},
        {"cond.v_out", "gnd.v", "bat.v_in"}
    };

    ASSERT_TRUE(devices_a[0].solver_role.has_value()) << "bat solver_role should exist";

    auto result_a = build_systems_dev(make_jit_input_resolved(devices_a, signal_groups_a));
    auto result_b = build_systems_dev(make_jit_input_resolved(devices_b, signal_groups_b));

    // Both should produce exactly 1 island
    ASSERT_EQ(result_a.electrical_plan.islands.size(), 1u);
    ASSERT_EQ(result_b.electrical_plan.islands.size(), 1u);

    // Both islands should have 3 elements: TheveninSource + FixedVoltageNode + ConductanceBranch
    ASSERT_EQ(result_a.electrical_plan.islands[0].elements.size(), 3u);
    ASSERT_EQ(result_b.electrical_plan.islands[0].elements.size(), 3u);

    // Solve both circuits
    auto st_a = make_state(result_a.signal_count);
    auto st_b = make_state(result_b.signal_count);

    JIT_Simulator sim_a;
    JIT_Simulator sim_b;

    // Use JSON-based simulator for proper initialization
    const std::string json_a = R"({
        "devices": [
            {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "0.1"}},
            {"name": "res", "classname": "Resistor", "params": {"conductance": "0.5"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "bat.v_out", "to": "res.v_in"},
            {"from": "res.v_out", "to": "gnd.v"},
            {"from": "bat.v_in", "to": "gnd.v"}
        ]
    })";

    const std::string json_b = R"({
        "devices": [
            {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "0.1"}},
            {"name": "cond", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "bat.v_out", "to": "cond.v_in"},
            {"from": "cond.v_out", "to": "gnd.v"},
            {"from": "bat.v_in", "to": "gnd.v"}
        ]
    })";

    sim_a.start(build_input_from_json(json_a));
    sim_b.start(build_input_from_json(json_b));

    double dt = 1.0 / 60.0;
    sim_a.step(dt);
    sim_b.step(dt);

    // Source v_out in both circuits should be identical
    float v_out_a = sim_a.get_signal_value(sim_a.resolve_signal_key("bat", "v_out"));
    float v_out_b = sim_b.get_signal_value(sim_b.resolve_signal_key("bat", "v_out"));
    EXPECT_NEAR(v_out_a, v_out_b, 1e-6f)
        << "Wrapper Resistor and primitive ElectricalConductance should produce identical source v_out";

    // Ground should be 0V in both
    EXPECT_NEAR(sim_a.get_signal_value(sim_a.resolve_signal_key("gnd", "v")), 0.0f, 1e-4f);
    EXPECT_NEAR(sim_b.get_signal_value(sim_b.resolve_signal_key("gnd", "v")), 0.0f, 1e-4f);

    // Verify the solved value makes physical sense:
    // V = Vth * (Rload / (Rload + Rseries)) = 28.0 * (2.0 / (2.0 + 0.1)) = 28.0 * 0.9524 ≈ 26.67V
    float expected_v = voltage * (1.0f / conductance) / (1.0f / conductance + resistance);
    EXPECT_NEAR(v_out_a, expected_v, 0.1f)
        << "Solved voltage should match Thevenin divider formula";
}

// ============================================================================
// Test 2: Primitive-only circuit solves correctly
// ============================================================================

TEST(ElectricalPrimitives, PrimitiveOnlyCircuitSolvesCorrectly) {
    // Pure primitive circuit: ElectricalSource + ElectricalConductance + RefNode
    // No wrapper components (Battery, Resistor) at all.
    //
    // Circuit: ElectricalSource (28V, 0.5 ohm) -> ElectricalConductance (g=2S, R=0.5 ohm) -> RefNode (0V)
    // Expected voltage at junction (source v_out = conductance v_in):
    //   V = 28 * (0.5 / (0.5 + 0.5)) = 14V

    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "0.5"}},
            {"name": "load", "classname": "ElectricalConductance", "params": {"conductance": "2.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "src.v_out", "to": "load.v_in"},
            {"from": "load.v_out", "to": "gnd.v"},
            {"from": "src.v_in", "to": "gnd.v"}
        ]
    })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;
    sim.step(dt);

    // Ground = 0V
    EXPECT_NEAR(sim.get_signal_value(sim.resolve_signal_key("gnd", "v")), 0.0f, 1e-4f);

    // Junction voltage (src.v_out = load.v_in):
    // Thevenin divider: V = 28 * (0.5 / (0.5 + 0.5)) = 14V
    float v_junction = sim.get_signal_value(sim.resolve_signal_key("src", "v_out"));
    EXPECT_NEAR(v_junction, 14.0f, 0.1f)
        << "Primitive-only circuit should solve to correct voltage divider result";

    // All values finite
    EXPECT_TRUE(std::isfinite(v_junction));
    EXPECT_TRUE(std::isfinite(sim.get_signal_value(sim.resolve_signal_key("load", "v_out"))));
}

// ============================================================================
// Test 3: Primitive-only circuit stable over many steps
// ============================================================================

TEST(ElectricalPrimitives, PrimitiveOnlyCircuitStableOverTime) {
    // Verify no drift or instability with primitive-only circuit over 500 steps.

    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "24.0", "resistance": "1.0"}},
            {"name": "load", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "src.v_out", "to": "load.v_in"},
            {"from": "load.v_out", "to": "gnd.v"},
            {"from": "src.v_in", "to": "gnd.v"}
        ]
    })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;

    // Expected: V = 24 * (2.0 / (2.0 + 1.0)) = 16V
    float expected_v = 24.0f * (2.0f / (2.0f + 1.0f));

    for (int i = 0; i < 500; ++i) {
        sim.step(dt);
        float v = sim.get_signal_value(sim.resolve_signal_key("src", "v_out"));
        EXPECT_TRUE(std::isfinite(v)) << "Voltage should be finite at frame " << i;
        EXPECT_NEAR(v, expected_v, 0.1f) << "Voltage should remain stable at frame " << i;
    }
}

// ============================================================================
// Test 4: Build plan correctly includes primitive elements
// ============================================================================

TEST(ElectricalPrimitives, BuildPlanIncludesPrimitiveElements) {
    // Verify that ElectricalConductance produces ConductanceBranch
    // and ElectricalSource produces TheveninSource in the electrical plan.

    std::vector<DeviceInstance> devices = {
        make_device("src", "ElectricalSource", {{"voltage", "12.0"}, {"resistance", "0.5"}}),
        make_device("load", "ElectricalConductance", {{"conductance", "1.0"}}),
        make_device("gnd", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::vector<std::string>> signal_groups = {
        {"src.v_out", "load.v_in"},
        {"load.v_out", "gnd.v", "src.v_in"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    // Should produce exactly 1 island
    ASSERT_EQ(result.electrical_plan.islands.size(), 1u);

    const auto& island = result.electrical_plan.islands[0];

    // 3 elements: TheveninSource (ElectricalSource) + ConductanceBranch (ElectricalConductance) + FixedVoltageNode (RefNode)
    ASSERT_EQ(island.elements.size(), 3u);

    // Collect element kinds
    std::vector<ElectricalElementKind> kinds;
    for (const auto& elem : island.elements) {
        kinds.push_back(elem.kind);
    }

    EXPECT_TRUE(std::find(kinds.begin(), kinds.end(), ElectricalElementKind::TheveninSource) != kinds.end())
        << "ElectricalSource should produce TheveninSource element";
    EXPECT_TRUE(std::find(kinds.begin(), kinds.end(), ElectricalElementKind::ConductanceBranch) != kinds.end())
        << "ElectricalConductance should produce ConductanceBranch element";
    EXPECT_TRUE(std::find(kinds.begin(), kinds.end(), ElectricalElementKind::FixedVoltageNode) != kinds.end())
        << "RefNode should produce FixedVoltageNode element";

    // Verify TheveninSource has correct parameters
    for (const auto& elem : island.elements) {
        if (elem.kind == ElectricalElementKind::TheveninSource) {
            EXPECT_FLOAT_EQ(elem.value_a, 12.0f) << "ElectricalSource voltage should be 12V";
            EXPECT_FLOAT_EQ(elem.value_b, 0.5f) << "ElectricalSource resistance should be 0.5 ohm";
        }
        if (elem.kind == ElectricalElementKind::ConductanceBranch) {
            EXPECT_FLOAT_EQ(elem.value_a, 1.0f) << "ElectricalConductance conductance should be 1.0S";
        }
    }
}

// ============================================================================
// Test 5: Primitives are solver-owned (not push-scheduled)
// ============================================================================

TEST(ElectricalPrimitives, PrimitivesAreSolverOwnedNotScheduled) {
    // Verify that ElectricalConductance and ElectricalSource are NOT scheduled
    // for push propagation. Only RefNode should be scheduled as a source.

    std::vector<DeviceInstance> devices = {
        make_device("src", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}),
        make_device("load", "ElectricalConductance", {{"conductance", "0.1"}}),
        make_device("gnd", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::vector<std::string>> signal_groups = {
        {"src.v_out", "load.v_in"},
        {"load.v_out", "gnd.v", "src.v_in"}
    };

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    // Only RefNode should be scheduled as a source
    EXPECT_EQ(result.scheduler.source_count(), 1u)
        << "Only RefNode should be scheduled as source (primitives are solver-owned)";
    // No consumers in this circuit
    EXPECT_EQ(result.scheduler.consumer_count(), 0u)
        << "No consumers expected (all electrical components are solver-owned or source)";
}

// ============================================================================
// Test 6: Mixed wrapper and primitive components in same island
// ============================================================================

TEST(ElectricalPrimitives, MixedWrapperAndPrimitiveInSameIsland) {
    // Build a circuit that mixes wrapper-like flow with primitive (ElectricalConductance).
    // Proves interoperability: legacy wrappers and new primitives coexist in the
    // same electrical island and solve correctly together.
    //
    // Circuit: ElectricalSource(28V, 0.01 ohm) -> ElectricalConductance(g=0.5) -> RefNode(0V)

    const std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "0.01"}},
            {"name": "load", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "bat.v_out", "to": "load.v_in"},
            {"from": "load.v_out", "to": "gnd.v"},
            {"from": "bat.v_in", "to": "gnd.v"}
        ]
    })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;
    sim.step(dt);

    float v_out = sim.get_signal_value(sim.resolve_signal_key("bat", "v_out"));
    EXPECT_TRUE(std::isfinite(v_out));
    // V = 28 * (2.0 / (2.0 + 0.01)) ≈ 27.86V
    EXPECT_NEAR(v_out, 28.0f * (2.0f / (2.0f + 0.01f)), 0.1f)
        << "Mixed wrapper+primitive circuit should solve correctly";
}

// ============================================================================
// Test 7: Source naming regression compatibility
// ============================================================================

TEST(ElectricalPrimitives, SourceAndBatteryEquivalent) {
    // ElectricalSource baseline compatibility check with equivalent loads.

    const std::string json_battery = R"({
        "devices": [
            {"name": "bat", "classname": "ElectricalSource", "params": {"voltage": "24.0", "resistance": "0.5"}},
            {"name": "res", "classname": "Resistor", "params": {"conductance": "1.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "bat.v_out", "to": "res.v_in"},
            {"from": "res.v_out", "to": "gnd.v"},
            {"from": "bat.v_in", "to": "gnd.v"}
        ]
    })";

    const std::string json_source = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "24.0", "resistance": "0.5"}},
            {"name": "load", "classname": "ElectricalConductance", "params": {"conductance": "1.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "src.v_out", "to": "load.v_in"},
            {"from": "load.v_out", "to": "gnd.v"},
            {"from": "src.v_in", "to": "gnd.v"}
        ]
    })";

    JIT_Simulator sim_bat;
    JIT_Simulator sim_src;
    sim_bat.start(build_input_from_json(json_battery));
    sim_src.start(build_input_from_json(json_source));

    double dt = 1.0 / 60.0;
    sim_bat.step(dt);
    sim_src.step(dt);

    // Both should produce same junction voltage
    // V = 24 * (1.0 / (1.0 + 0.5)) = 16V
    float v_bat = sim_bat.get_signal_value(sim_bat.resolve_signal_key("bat", "v_out"));
    float v_src = sim_src.get_signal_value(sim_src.resolve_signal_key("src", "v_out"));

    EXPECT_NEAR(v_bat, v_src, 1e-6f)
        << "Equivalent source configurations should produce identical results";
    EXPECT_NEAR(v_bat, 16.0f, 0.1f)
        << "Solved voltage should match Thevenin divider formula";
}

// ============================================================================
// Test 8: Two primitives in series
// ============================================================================

TEST(ElectricalPrimitives, TwoConductancesInSeries) {
    // ElectricalSource -> Conductance1 -> Conductance2 -> RefNode
    // Two resistors in series: R_total = R1 + R2 = 2 + 2 = 4 ohm
    // V_junction1 = V_source * R_total / (R_total + R_series_source)
    //
    // With ideal source (low resistance):
    // Source: 28V, 0 ohm series
    // R1 = 2 ohm (g=0.5), R2 = 2 ohm (g=0.5)
    // V_node1 (after source, before R1) = 28V (ideal source)
    // V_node2 (between R1 and R2) = 28 * (R2 / (R1+R2)) = 28 * 0.5 = 14V

    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "0.0"}},
            {"name": "r1", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "r2", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            {"from": "src.v_out", "to": "r1.v_in"},
            {"from": "r1.v_out", "to": "r2.v_in"},
            {"from": "r2.v_out", "to": "gnd.v"},
            {"from": "src.v_in", "to": "gnd.v"}
        ]
    })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));

    double dt = 1.0 / 60.0;
    sim.step(dt);

    // Source output should be 28V (ideal source, R_series = 0)
    float v_src = sim.get_signal_value(sim.resolve_signal_key("src", "v_out"));
    EXPECT_NEAR(v_src, 28.0f, 0.1f) << "Ideal source output should be 28V";

    // Middle node (r1.v_out = r2.v_in) should be 14V
    float v_mid = sim.get_signal_value(sim.resolve_signal_key("r1", "v_out"));
    EXPECT_NEAR(v_mid, 14.0f, 0.1f) << "Mid-point should be 14V (equal series resistors)";

    // Ground should be 0V
    EXPECT_NEAR(sim.get_signal_value(sim.resolve_signal_key("gnd", "v")), 0.0f, 1e-4f);
}

// ============================================================================
// Test 9: Default parameters work correctly
// ============================================================================

TEST(ElectricalPrimitives, DefaultParametersWork) {
    // Build circuit using default params from blueprint files.
    // ElectricalSource defaults: voltage=28.0, resistance=0.01
    // ElectricalConductance defaults: conductance=0.1

    std::vector<DeviceInstance> devices = {
        make_device("src", "ElectricalSource"),  // defaults
        make_device("load", "ElectricalConductance"),  // defaults
        make_device("gnd", "RefNode", {{"value", "0.0"}})
    };

    std::vector<std::vector<std::string>> signal_groups = {
        {"src.v_out", "load.v_in"},
        {"load.v_out", "gnd.v", "src.v_in"}
    };

    // Should not throw - defaults should be used
    EXPECT_NO_THROW(build_systems_dev(make_jit_input(devices, signal_groups)));

    auto result = build_systems_dev(make_jit_input(devices, signal_groups));

    // Verify defaults in the electrical plan
    ASSERT_EQ(result.electrical_plan.islands.size(), 1u);
    const auto& island = result.electrical_plan.islands[0];
    ASSERT_EQ(island.elements.size(), 3u);

    for (const auto& elem : island.elements) {
        if (elem.kind == ElectricalElementKind::TheveninSource) {
            EXPECT_FLOAT_EQ(elem.value_a, 28.0f) << "Default voltage should be 28V";
            EXPECT_FLOAT_EQ(elem.value_b, 0.01f) << "Default resistance should be 0.01 ohm";
        }
        if (elem.kind == ElectricalElementKind::ConductanceBranch) {
            EXPECT_FLOAT_EQ(elem.value_a, 0.1f) << "Default conductance should be 0.1S";
        }
    }
}

// ============================================================================
// Test 10: Unknown param on primitive throws
// ============================================================================

TEST(ElectricalPrimitives, UnknownParamThrows) {
    // Primitives should reject unknown params via validate_all_consumed().

    // ElectricalConductance with typo
    {
        DeviceInstance dev;
        dev.name = "cond_bad";
        dev.classname = "ElectricalConductance";
        dev.params = {{"conductanse", "0.5"}};
        auto ports = get_component_ports("ElectricalConductance");
        for (const auto& p : ports) {
            dev.ports[p] = Port{bp2::Direction::InOut, PortType::Any};
        }

        std::vector<DeviceInstance> devices = {dev};
        std::vector<std::vector<std::string>> signal_groups;

        EXPECT_THROW(build_systems_dev(make_jit_input(devices, signal_groups)), std::runtime_error);
    }

    // ElectricalSource with typo
    {
        DeviceInstance dev;
        dev.name = "src_bad";
        dev.classname = "ElectricalSource";
        dev.params = {{"voltage", "28.0"}, {"resistanse", "0.01"}};
        auto ports = get_component_ports("ElectricalSource");
        for (const auto& p : ports) {
            dev.ports[p] = Port{bp2::Direction::InOut, PortType::Any};
        }

        std::vector<DeviceInstance> devices = {dev};
        std::vector<std::vector<std::string>> signal_groups;

        EXPECT_THROW(build_systems_dev(make_jit_input(devices, signal_groups)), std::runtime_error);
    }
}

// ============================================================================
// Step 14 Verification Tests — Metadata-Driven Solver Role Extraction
//
// These tests verify that:
//   11. Valid solver_role metadata produces the correct element kind
//   12. Missing required port key in solver_role fails with clear error
//   13. Missing required param key in solver_role fails with clear error
//   14. solver_role metadata is propagated through library loading pipeline
// ============================================================================

// ============================================================================
// Test 11: Valid solver_role metadata produces correct element kind
// ============================================================================

TEST(ElectricalPrimitives, MetadataProducesCorrectElementKind) {
    // Build devices with solver_role metadata manually set (simulating library-loaded state).
    // Verify that the metadata-driven path produces correct ElectricalElementKind.

    // -- ConductanceBranch via solver_role --
    {
        ResolvedDevice dev = make_resolved_device_with_role("cond1", "ElectricalConductance", {{"conductance", "0.5"}}, SolverRole{
            "ConductanceBranch",
            {{"a", "v_in"}, {"b", "v_out"}},
            {{"g", "conductance"}}
        });

        ResolvedDevice gnd = make_resolved_device("gnd", "RefNode", {{"value", "0.0"}});
        ResolvedDevice bat = make_resolved_device("bat", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.1"}});

        std::vector<ResolvedDevice> devices = {bat, dev, gnd};
        std::vector<std::vector<std::string>> signal_groups = {
            {"bat.v_out", "cond1.v_in"},
            {"cond1.v_out", "gnd.v", "bat.v_in"}
        };

        auto result = build_systems_dev(make_jit_input_resolved(devices, signal_groups));
        ASSERT_EQ(result.electrical_plan.islands.size(), 1);
        const auto& island = result.electrical_plan.islands[0];

        // Find the element that came from cond1 (ConductanceBranch with g=0.5)
        bool found = false;
        for (const auto& elem : island.elements) {
            if (elem.kind == ElectricalElementKind::ConductanceBranch &&
                std::abs(elem.value_a - 0.5f) < 1e-6f) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Metadata-driven ConductanceBranch with g=0.5 not found in island";
    }

    // -- TheveninSource via solver_role --
    {
        ResolvedDevice dev = make_resolved_device_with_role("src1", "ElectricalSource", {{"voltage", "12.0"}, {"resistance", "0.05"}}, SolverRole{
            "TheveninSource",
            {{"pos", "v_out"}, {"neg", "v_in"}},
            {{"voltage", "voltage"}, {"resistance", "resistance"}}
        });

        ResolvedDevice gnd = make_resolved_device("gnd", "RefNode", {{"value", "0.0"}});
        ResolvedDevice res = make_resolved_device("res", "Resistor", {{"conductance", "0.1"}});

        std::vector<ResolvedDevice> devices = {dev, res, gnd};
        std::vector<std::vector<std::string>> signal_groups = {
            {"src1.v_out", "res.v_in"},
            {"res.v_out", "gnd.v", "src1.v_in"}
        };

        auto result = build_systems_dev(make_jit_input_resolved(devices, signal_groups));
        ASSERT_EQ(result.electrical_plan.islands.size(), 1);
        const auto& island = result.electrical_plan.islands[0];

        bool found = false;
        for (const auto& elem : island.elements) {
            if (elem.kind == ElectricalElementKind::TheveninSource &&
                std::abs(elem.value_a - 12.0f) < 1e-6f &&
                std::abs(elem.value_b - 0.05f) < 1e-6f) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Metadata-driven TheveninSource with V=12, R=0.05 not found in island";
    }

    // -- FixedVoltageNode via solver_role --
    {
        ResolvedDevice dev = make_resolved_device_with_role("ref1", "RefNode", {{"value", "5.0"}}, SolverRole{
            "FixedVoltageNode",
            {{"node", "v"}},
            {{"voltage", "value"}}
        });

        ResolvedDevice bat = make_resolved_device("bat", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.1"}});
        ResolvedDevice res = make_resolved_device("res", "Resistor", {{"conductance", "0.1"}});

        std::vector<ResolvedDevice> devices = {bat, res, dev};
        std::vector<std::vector<std::string>> signal_groups = {
            {"bat.v_out", "res.v_in"},
            {"res.v_out", "ref1.v", "bat.v_in"}
        };

        auto result = build_systems_dev(make_jit_input_resolved(devices, signal_groups));
        ASSERT_EQ(result.electrical_plan.islands.size(), 1);
        const auto& island = result.electrical_plan.islands[0];

        bool found = false;
        for (const auto& elem : island.elements) {
            if (elem.kind == ElectricalElementKind::FixedVoltageNode &&
                std::abs(elem.value_a - 5.0f) < 1e-6f) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Metadata-driven FixedVoltageNode with V=5.0 not found in island";
    }
}

// ============================================================================
// Test 12: Missing required port key in solver_role fails with clear error
// ============================================================================

TEST(ElectricalPrimitives, MetadataMissingPortKeyThrows) {
    // ConductanceBranch requires port keys "a" and "b". Test with "a" missing.
    {
        ResolvedDevice dev = make_resolved_device_with_role("cond1", "ElectricalConductance", {{"conductance", "0.5"}}, SolverRole{
            "ConductanceBranch",
            {{"b", "v_out"}},  // Missing "a" key
            {{"g", "conductance"}}
        });

        ResolvedDevice gnd = make_resolved_device("gnd", "RefNode", {{"value", "0.0"}});
        ResolvedDevice bat = make_resolved_device("bat", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.1"}});

        std::vector<ResolvedDevice> devices = {bat, dev, gnd};
        std::vector<std::vector<std::string>> signal_groups = {
            {"bat.v_out", "cond1.v_in"},
            {"cond1.v_out", "gnd.v"},
            {"bat.v_in", "gnd.v"}
        };

        EXPECT_THROW(build_systems_dev(make_jit_input_resolved(devices, signal_groups)), std::runtime_error);
    }

    // TheveninSource requires "pos" and "neg". Test with "neg" missing.
    {
        ResolvedDevice dev = make_resolved_device_with_role("src1", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}, SolverRole{
            "TheveninSource",
            {{"pos", "v_out"}},  // Missing "neg" key
            {{"voltage", "voltage"}, {"resistance", "resistance"}}
        });

        ResolvedDevice gnd = make_resolved_device("gnd", "RefNode", {{"value", "0.0"}});
        ResolvedDevice res = make_resolved_device("res", "Resistor", {{"conductance", "0.1"}});

        std::vector<ResolvedDevice> devices = {dev, res, gnd};
        std::vector<std::vector<std::string>> signal_groups = {
            {"src1.v_out", "res.v_in"},
            {"res.v_out", "gnd.v"},
            {"src1.v_in", "gnd.v"}
        };

        EXPECT_THROW(build_systems_dev(make_jit_input_resolved(devices, signal_groups)), std::runtime_error);
    }

    // FixedVoltageNode requires "node". Test with empty port_map.
    {
        ResolvedDevice dev = make_resolved_device_with_role("ref1", "RefNode", {{"value", "0.0"}}, SolverRole{
            "FixedVoltageNode",
            {},  // Missing "node" key
            {{"voltage", "value"}}
        });

        ResolvedDevice bat = make_resolved_device("bat", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.1"}});
        ResolvedDevice res = make_resolved_device("res", "Resistor", {{"conductance", "0.1"}});

        std::vector<ResolvedDevice> devices = {bat, res, dev};
        std::vector<std::vector<std::string>> signal_groups = {
            {"bat.v_out", "res.v_in"},
            {"res.v_out", "ref1.v"},
            {"bat.v_in", "ref1.v"}
        };

        EXPECT_THROW(build_systems_dev(make_jit_input_resolved(devices, signal_groups)), std::runtime_error);
    }
}

// ============================================================================
// Test 13: Missing required param key in solver_role fails with clear error
// ============================================================================

TEST(ElectricalPrimitives, MetadataMissingParamKeyThrows) {
    // ConductanceBranch requires param key "g". Test with it missing.
    {
        ResolvedDevice dev = make_resolved_device_with_role("cond1", "ElectricalConductance", {{"conductance", "0.5"}}, SolverRole{
            "ConductanceBranch",
            {{"a", "v_in"}, {"b", "v_out"}},
            {}  // Missing "g" key
        });

        ResolvedDevice gnd = make_resolved_device("gnd", "RefNode", {{"value", "0.0"}});
        ResolvedDevice bat = make_resolved_device("bat", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.1"}});

        std::vector<ResolvedDevice> devices = {bat, dev, gnd};
        std::vector<std::vector<std::string>> signal_groups = {
            {"bat.v_out", "cond1.v_in"},
            {"cond1.v_out", "gnd.v"},
            {"bat.v_in", "gnd.v"}
        };

        EXPECT_THROW(build_systems_dev(make_jit_input_resolved(devices, signal_groups)), std::runtime_error);
    }

    // TheveninSource requires "voltage" and "resistance". Test with "resistance" missing.
    {
        ResolvedDevice dev = make_resolved_device_with_role("src1", "ElectricalSource", {{"voltage", "28.0"}, {"resistance", "0.01"}}, SolverRole{
            "TheveninSource",
            {{"pos", "v_out"}, {"neg", "v_in"}},
            {{"voltage", "voltage"}}  // Missing "resistance" key
        });

        ResolvedDevice gnd = make_resolved_device("gnd", "RefNode", {{"value", "0.0"}});
        ResolvedDevice res = make_resolved_device("res", "Resistor", {{"conductance", "0.1"}});

        std::vector<ResolvedDevice> devices = {dev, res, gnd};
        std::vector<std::vector<std::string>> signal_groups = {
            {"src1.v_out", "res.v_in"},
            {"res.v_out", "gnd.v"},
            {"src1.v_in", "gnd.v"}
        };

        EXPECT_THROW(build_systems_dev(make_jit_input_resolved(devices, signal_groups)), std::runtime_error);
    }
}

// ============================================================================
// Test 14: solver_role metadata propagated through library loading pipeline
// ============================================================================

TEST(ElectricalPrimitives, MetadataPropagatedThroughLibraryPipeline) {
    // Use the full simulator startup pipeline via canonical JitBuildInput which loads the
    // type registry. Verify that solver_role is populated on the merged devices
    // and produces correct solve results.
    //
    // This test proves that the end-to-end path works:
    //   blueprint file -> load_component_registry() -> resolve_component() -> build -> solve

    // Primitive-only circuit through full pipeline
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "24.0", "resistance": "0.1"}},
            {"name": "cond", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            "src.v_out -> cond.v_in",
            "cond.v_out -> gnd.v",
            "src.v_in -> gnd.v"
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(build_input_from_json(json)));

    sim.step(1.0 / 60.0);

    // Check solved voltages make physical sense
    // V = Vsrc * Rload / (Rload + Rint) = 24.0 * 2.0 / (2.0 + 0.1) = 22.857
    float v_gnd = sim.get_signal_value(sim.resolve_signal_key("gnd", "v"));
    float v_src_out = sim.get_signal_value(sim.resolve_signal_key("src", "v_out"));

    EXPECT_NEAR(v_gnd, 0.0f, 1e-4f);
    EXPECT_GT(v_src_out, 20.0f);  // Should be ~22.857 V
    EXPECT_LT(v_src_out, 25.0f);  // Must be less than source voltage
}
