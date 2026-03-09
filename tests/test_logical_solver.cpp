#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "jit_solver/simulator.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/visual/scene/persist.h"

/// TDD: Logical Solver and Comparator Component
/// These tests are written FIRST (TDD approach) and will FAIL until implementation is complete

// =============================================================================
// Tests for Logical Domain
// =============================================================================

TEST(LogicalSolverTest, LogicalDomain_ExistsInEnum) {
    // Logical domain should be defined in Domain enum
    using namespace an24;

    // This test verifies that Domain::Logical compiles
    Domain d = Domain::Logical;
    (void)d;  // Suppress unused warning
    SUCCEED();
}

TEST(LogicalSolverTest, LogicalDomain_ParsesFromString) {
    // Logical domain should parse from JSON string "Logical"
    using namespace an24;

    // Try to load component registry (which will parse domains)
    // If "Logical" domain doesn't parse, this will fail
    TypeRegistry registry = load_type_registry("library/");

    // Verify that we loaded at least the Comparator component
    const auto* comp = registry.get("Comparator");
    if (comp) {
        ASSERT_TRUE(comp->domains.has_value())
            << "Comparator should have default domains defined";

        auto& domains = comp->domains.value();
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
    using namespace an24;

    TypeRegistry registry = load_type_registry("library/");

    const auto* comp = registry.get("Comparator");
    if (!comp) {
        // This is expected to fail until we implement the component
        FAIL() << "Comparator component not found in registry - needs to be created in components/Comparator.json";
        return;
    }

    // Check description
    EXPECT_FALSE(comp->description.empty()) << "Comparator should have a description";

    // Check ports exist (Von and Voff are parameters, not ports!)
    EXPECT_TRUE(comp->ports.contains("Va")) << "Should have Va input";
    EXPECT_TRUE(comp->ports.contains("Vb")) << "Should have Vb input";
    EXPECT_TRUE(comp->ports.contains("o")) << "Should have o output";

    // Check port directions
    EXPECT_EQ(comp->ports.at("Va").direction, PortDirection::In);
    EXPECT_EQ(comp->ports.at("Vb").direction, PortDirection::In);
    EXPECT_EQ(comp->ports.at("o").direction, PortDirection::Out);

    // Check port types (Va, Vb should be Voltage; o should be Bool)
    EXPECT_EQ(comp->ports.at("Va").type, PortType::V);
    EXPECT_EQ(comp->ports.at("Vb").type, PortType::V);
    EXPECT_EQ(comp->ports.at("o").type, PortType::Bool);

    // Check parameters
    EXPECT_TRUE(comp->params.contains("Von")) << "Should have Von parameter";
    EXPECT_TRUE(comp->params.contains("Voff")) << "Should have Voff parameter";
}

TEST(LogicalSolverTest, Comparator_InLogicalDomain) {
    // Comparator should be registered in Logical domain
    using namespace an24;

    TypeRegistry registry = load_type_registry("library/");

    const auto* comp = registry.get("Comparator");
    if (!comp) {
        FAIL() << "Comparator component not found in registry";
        return;
    }

    ASSERT_TRUE(comp->domains.has_value())
        << "Comparator should have default domains defined";

    auto& domains = comp->domains.value();
    bool has_logical = std::find(domains.begin(), domains.end(), Domain::Logical) != domains.end();
    EXPECT_TRUE(has_logical) << "Comparator should be in Logical domain";
}

// =============================================================================
// Tests for Logical Solver Integration
// =============================================================================

TEST(LogicalSolverTest, LogicalSolver_HasLogicalVector) {
    // Systems should have a logical component vector
    using namespace an24;

    // This test will compile but we can't fully test it without
    // actually creating a blueprint with a Comparator component
    // For now, just verify the code compiles with Logical domain

    Systems systems;
    (void)systems;  // Suppress unused warning

    SUCCEED() << "Systems class compiles with Logical domain support";
}

TEST(LogicalSolverTest, Component_HasSolveLogicalMethod) {
    // Component base class should have solve_logical method
    using namespace an24;

    // This is a compile-time test - if solve_logical doesn't exist,
    // this won't compile
    class MockComponent : public Component {
    public:
        std::string_view type_name() const override { return "Mock"; }

        // Override solve_logical (should compile)
        void solve_logical(SimulationState& state, float dt) override {
            (void)state;
            (void)dt;
        }
    };

    MockComponent mock;
    (void)mock;  // Suppress unused warning

    SUCCEED() << "Component::solve_logical method exists";
}

// =============================================================================
// Tests for Hysteresis Behavior (Integration Tests)
// =============================================================================

TEST(LogicalSolverTest, Comparator_Hysteresis_BasicBehavior) {
    // Test basic hysteresis: output turns ON above Von, OFF below Voff
    // Using default params: Von=5.0, Voff=2.0
    using namespace an24;

    Blueprint bp;

    // Create comparator node
    Node comp;
    comp.id = "comp1";
    comp.name = "Comparator";
    comp.type_name = "Comparator";
    comp.kind = NodeKind::Node;
    comp.at(0, 0);
    comp.input("Va");
    comp.input("Vb");
    comp.output("o");
    bp.add_node(std::move(comp));

    // Create simulator and start
    Simulator<JIT_Solver> simulator;
    simulator.start(bp);

    // Test 1: Initial state (all zeros) -> output FALSE (diff=0, not > Von)
    simulator.step(0.016f);  // 60Hz = 16.67ms
    bool output1 = simulator.get_component_state_as_bool("comp1", "o");
    EXPECT_FALSE(output1) << "Initial output should be FALSE when all inputs are zero";

    // Test 2: Set Va=10, Vb=0 -> (10-0) > Von(5) -> TRUE
    simulator.apply_overrides({{"comp1.Va", 10.0f}, {"comp1.Vb", 0.0f}});
    simulator.step(0.016f);
    bool output2 = simulator.get_component_state_as_bool("comp1", "o");
    EXPECT_TRUE(output2) << "Output should be TRUE when (Va - Vb) > Von";

    // Test 3: Reduce Va to 4 -> (4-0) = 4, in hysteresis band [2, 5] -> maintain TRUE
    simulator.apply_overrides({{"comp1.Va", 4.0f}});
    simulator.step(0.016f);
    bool output3 = simulator.get_component_state_as_bool("comp1", "o");
    EXPECT_TRUE(output3) << "Output should maintain TRUE when in hysteresis band";

    // Test 4: Reduce Va to 1 -> (1-0) < Voff(2) -> FALSE
    simulator.apply_overrides({{"comp1.Va", 1.0f}});
    simulator.step(0.016f);
    bool output4 = simulator.get_component_state_as_bool("comp1", "o");
    EXPECT_FALSE(output4) << "Output should be FALSE when (Va - Vb) < Voff";

    // Test 5: Increase Va to 3 -> (3-0) = 3, in hysteresis band -> maintain FALSE
    simulator.apply_overrides({{"comp1.Va", 3.0f}});
    simulator.step(0.016f);
    bool output5 = simulator.get_component_state_as_bool("comp1", "o");
    EXPECT_FALSE(output5) << "Output should maintain FALSE when in hysteresis band";

    // Test 6: Increase Va to 6 -> (6-0) > Von(5) -> TRUE
    simulator.apply_overrides({{"comp1.Va", 6.0f}});
    simulator.step(0.016f);
    bool output6 = simulator.get_component_state_as_bool("comp1", "o");
    EXPECT_TRUE(output6) << "Output should be TRUE when (Va - Vb) > Von";

    simulator.stop();
}

TEST(LogicalSolverTest, Comparator_Hysteresis_WithVbOffset) {
    // Test hysteresis with non-zero Vb
    using namespace an24;

    Blueprint bp;
    Node comp;
    comp.id = "comp1";
    comp.name = "Comparator";
    comp.type_name = "Comparator";
    comp.kind = NodeKind::Node;
    comp.at(0, 0);
    comp.input("Va");
    comp.input("Vb");
    comp.output("o");
    bp.add_node(std::move(comp));

    Simulator<JIT_Solver> simulator;
    simulator.start(bp);

    // Using default params: Von=5, Voff=2
    // Set Vb=10, Va=15 -> (15-10) = 5, at Von threshold -> TRUE
    simulator.apply_overrides({
        {"comp1.Vb", 10.0f},
        {"comp1.Va", 15.0f}
    });
    simulator.step(0.016f);
    bool output1 = simulator.get_component_state_as_bool("comp1", "o");
    EXPECT_TRUE(output1) << "At Von threshold, output should be TRUE";

    // Va=12 -> (12-10) = 2, at Voff threshold -> FALSE
    simulator.apply_overrides({{"comp1.Va", 12.0f}});
    simulator.step(0.016f);
    bool output2 = simulator.get_component_state_as_bool("comp1", "o");
    EXPECT_FALSE(output2) << "At Voff threshold, output should be FALSE";

    simulator.stop();
}
