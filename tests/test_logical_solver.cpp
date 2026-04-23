#include <gtest/gtest.h>
#include "io/json/component_registry_json_loader.h"
#include "core/solvers/jit/simulator.h"

/// TDD: Logical Solver and Comparator Component
/// These tests are written FIRST (TDD approach) and will FAIL until implementation is complete

// =============================================================================
// Helper: build minimal simulator JSON for a single Comparator node
// =============================================================================

static std::string comparator_sim_json() {
    return R"({
  "templates": {},
  "devices": [
    {
      "name": "comp1",
      "template_name": "",
      "classname": "Comparator",
      "priority": "med",
      "bucket": null,
      "critical": false,
      "ports": {
        "Va": { "direction": "In", "type": "V" },
        "Vb": { "direction": "In", "type": "V" },
        "o":  { "direction": "Out", "type": "Bool" }
      }
    }
  ],
  "connections": []
})";
}

// =============================================================================
// Tests for Logical Domain
// =============================================================================

TEST(LogicalSolverTest, LogicalDomain_ExistsInEnum) {
    // Logical domain should be defined in Domain enum
    // This test verifies that Domain::Logical compiles
    Domain d = Domain::Logical;
    (void)d;  // Suppress unused warning
    SUCCEED();
}

TEST(LogicalSolverTest, LogicalDomain_ParsesFromString) {
    // Logical domain should parse from JSON string "Logical"
    // Try to load component registry (which will parse domains)
    // If "Logical" domain doesn't parse, this will fail
    ComponentRegistry registry = load_component_registry("library/");

    // Verify that we loaded at least the Comparator component
    const auto* comp = registry.get("Comparator");
    if (comp) {
        const auto& domains = spec_domains(*comp);
        bool has_logical = std::find(domains.begin(), domains.end(), Domain::Logical) != domains.end();
        EXPECT_TRUE(has_logical) << "Comparator should be in Logical domain";
    } else {
        // If component doesn't exist yet, this test will fail - that's OK for TDD
        FAIL() << "Comparator component not found in registry - needs to be implemented";
    }
}

// =============================================================================
// Tests for Comparator Component Definition
// =============================================================================

TEST(LogicalSolverTest, Comparator_TypeDefinitionExists) {
    // Component should be in registry with correct structure
    ComponentRegistry registry = load_component_registry("library/");

    const auto* comp = registry.get("Comparator");
    if (!comp) {
        // This is expected to fail until we implement the component
        FAIL() << "Comparator component not found in registry - needs to be created in components/Comparator.json";
        return;
    }

    // Check description
    const auto* pres = registry.presentation.get("Comparator");
    EXPECT_TRUE(pres && !pres->description.empty()) << "Comparator should have a description";

    // Check ports exist (Von and Voff are parameters, not ports!)
    EXPECT_TRUE(spec_ports(*comp).contains("Va")) << "Should have Va input";
    EXPECT_TRUE(spec_ports(*comp).contains("Vb")) << "Should have Vb input";
    EXPECT_TRUE(spec_ports(*comp).contains("o")) << "Should have o output";

    // Check port directions
    EXPECT_EQ(spec_ports(*comp).at("Va").direction, bp2::Direction::Input);
    EXPECT_EQ(spec_ports(*comp).at("Vb").direction, bp2::Direction::Input);
    EXPECT_EQ(spec_ports(*comp).at("o").direction, bp2::Direction::Output);

    // Check port types (Va, Vb accept Signal type; o should be Bool)
    EXPECT_EQ(spec_ports(*comp).at("Va").type, PortType::Signal);
    EXPECT_EQ(spec_ports(*comp).at("Vb").type, PortType::Signal);
    EXPECT_EQ(spec_ports(*comp).at("o").type, PortType::Bool);

    // Check parameters
    EXPECT_TRUE(spec_params(*comp).contains("Von")) << "Should have Von parameter";
    EXPECT_TRUE(spec_params(*comp).contains("Voff")) << "Should have Voff parameter";
}

TEST(LogicalSolverTest, Comparator_InLogicalDomain) {
    // Comparator should be registered in Logical domain
    ComponentRegistry registry = load_component_registry("library/");

    const auto* comp = registry.get("Comparator");
    if (!comp) {
        FAIL() << "Comparator component not found in registry";
        return;
    }

    const auto& domains = spec_domains(*comp);
    bool has_logical = std::find(domains.begin(), domains.end(), Domain::Logical) != domains.end();
    EXPECT_TRUE(has_logical) << "Comparator should be in Logical domain";
}

// =============================================================================
// Tests for Logical Solver Integration
// =============================================================================

TEST(LogicalSolverTest, LogicalSolver_HasLogicalVector) {
    // Logical-capable blueprint should build and run through Simulator.
    // This validates logical-path plumbing without legacy Systems API.
    Simulator<JIT_Solver> simulator;
    simulator.start(build_input_from_json(comparator_sim_json()));
    simulator.step(0.016f);
    simulator.stop();

    SUCCEED() << "Simulator logical path compiles and executes";
}

// =============================================================================
// Tests for Hysteresis Behavior (Integration Tests)
// =============================================================================

TEST(LogicalSolverTest, Comparator_Hysteresis_BasicBehavior) {
    // Test basic hysteresis: output turns ON above Von, OFF below Voff
    // Using default params: Von=5.0, Voff=2.0
    Simulator<JIT_Solver> simulator;
    simulator.start(build_input_from_json(comparator_sim_json()));

    // Test 1: Initial state (all zeros) -> output FALSE (diff=0, not > Von)
    simulator.step(0.016f);  // 60Hz = 16.67ms
    bool output1 = simulator.get_signal_value(simulator.resolve_signal_key("comp1", "o")) > 0.5f;
    EXPECT_FALSE(output1) << "Initial output should be FALSE when all inputs are zero";

    // Test 2: Set Va=10, Vb=0 -> (10-0) > Von(5) -> TRUE
    simulator.apply_typed_overrides({{simulator.signal_key_interner().lookup("comp1.Va"), 10.0f}, {simulator.signal_key_interner().lookup("comp1.Vb"), 0.0f}});
    simulator.step(0.016f);
    bool output2 = simulator.get_signal_value(simulator.resolve_signal_key("comp1", "o")) > 0.5f;
    EXPECT_TRUE(output2) << "Output should be TRUE when (Va - Vb) > Von";

    // Test 3: Reduce Va to 4 -> (4-0) = 4, in hysteresis band [2, 5] -> maintain TRUE
    simulator.apply_typed_overrides({{simulator.signal_key_interner().lookup("comp1.Va"), 4.0f}});
    simulator.step(0.016f);
    bool output3 = simulator.get_signal_value(simulator.resolve_signal_key("comp1", "o")) > 0.5f;
    EXPECT_TRUE(output3) << "Output should maintain TRUE when in hysteresis band";

    // Test 4: Reduce Va to 1 -> (1-0) < Voff(2) -> FALSE
    simulator.apply_typed_overrides({{simulator.signal_key_interner().lookup("comp1.Va"), 1.0f}});
    simulator.step(0.016f);
    bool output4 = simulator.get_signal_value(simulator.resolve_signal_key("comp1", "o")) > 0.5f;
    EXPECT_FALSE(output4) << "Output should be FALSE when (Va - Vb) < Voff";

    // Test 5: Increase Va to 3 -> (3-0) = 3, in hysteresis band -> maintain FALSE
    simulator.apply_typed_overrides({{simulator.signal_key_interner().lookup("comp1.Va"), 3.0f}});
    simulator.step(0.016f);
    bool output5 = simulator.get_signal_value(simulator.resolve_signal_key("comp1", "o")) > 0.5f;
    EXPECT_FALSE(output5) << "Output should maintain FALSE when in hysteresis band";

    // Test 6: Increase Va to 6 -> (6-0) > Von(5) -> TRUE
    simulator.apply_typed_overrides({{simulator.signal_key_interner().lookup("comp1.Va"), 6.0f}});
    simulator.step(0.016f);
    bool output6 = simulator.get_signal_value(simulator.resolve_signal_key("comp1", "o")) > 0.5f;
    EXPECT_TRUE(output6) << "Output should be TRUE when (Va - Vb) > Von";

    simulator.stop();
}

TEST(LogicalSolverTest, Comparator_Hysteresis_WithVbOffset) {
    // Test hysteresis with non-zero Vb
    Simulator<JIT_Solver> simulator;
    simulator.start(build_input_from_json(comparator_sim_json()));

    // Using default params: Von=5, Voff=2
    // Set Vb=10, Va=15 -> (15-10) = 5, at Von threshold -> TRUE
    simulator.apply_typed_overrides({
        {simulator.signal_key_interner().lookup("comp1.Vb"), 10.0f},
        {simulator.signal_key_interner().lookup("comp1.Va"), 15.0f}
    });
    simulator.step(0.016f);
    bool output1 = simulator.get_signal_value(simulator.resolve_signal_key("comp1", "o")) > 0.5f;
    EXPECT_TRUE(output1) << "At Von threshold, output should be TRUE";

    // Va=12 -> (12-10) = 2, at Voff threshold -> FALSE
    simulator.apply_typed_overrides({{simulator.signal_key_interner().lookup("comp1.Va"), 12.0f}});
    simulator.step(0.016f);
    bool output2 = simulator.get_signal_value(simulator.resolve_signal_key("comp1", "o")) > 0.5f;
    EXPECT_FALSE(output2) << "At Voff threshold, output should be FALSE";

    simulator.stop();
}
