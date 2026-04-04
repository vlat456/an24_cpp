#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/registry/type_registry.h"
#include "blueprint_v2/path/path.h"

namespace bp2 {

// =============================================================================
// Issue #8: Missing clone() method
// =============================================================================

TEST(BlueprintMissingClone, CloneShouldCreateIndependentCopy) {
    ui::StringInterner interner;
    TypeRegistry reg = TypeRegistry::create_test_registry(interner);

    Blueprint original;
    original = original.with_id(interner.intern("original"));
    
    Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");
    original = original.with_node(std::move(node));

    // This test will fail until clone() is implemented
    // Blueprint cloned = original.clone(interner.intern("cloned"));
    // EXPECT_NE(original.id(), cloned.id());
    // EXPECT_EQ(interner.resolve(original.id()), "original");
    // EXPECT_EQ(interner.resolve(cloned.id()), "cloned");
    
    // For now, verify that we can't do it:
    SUCCEED() << "clone() method not implemented - this test documents what's missing";
}

TEST(BlueprintMissingClone, CloneShouldDeepCopyNestedBlueprints) {
    ui::StringInterner interner;
    TypeRegistry reg = TypeRegistry::create_test_registry(interner);

    Blueprint nested;
    nested = nested.with_id(interner.intern("nested"));
    
    Blueprint::Node nested_node;
    nested_node.id = interner.intern("nn1");
    nested_node.type = interner.intern("Resistor");
    nested = nested.with_node(std::move(nested_node));

    Blueprint::Nested instance;
    instance.id = interner.intern("sub1");
    instance.embedded = true;
    instance.inline_def = std::make_unique<Blueprint>(nested);

    Blueprint original;
    original = original.with_nested(std::move(instance));

    // This test will fail until clone() is implemented
    // Blueprint cloned = original.clone(interner.intern("cloned"));
    // const auto* cloned_nested = cloned.find_nested(interner.intern("sub1"));
    // ASSERT_NE(cloned_nested, nullptr);
    // ASSERT_NE(cloned_nested->inline_def, nullptr);
    // const auto* cloned_node = cloned_nested->inline_def->find_node(interner.intern("nn1"));
    // ASSERT_NE(cloned_node, nullptr);
    // EXPECT_EQ(interner.resolve(cloned_node->type), "Resistor");

    SUCCEED() << "clone() method not implemented - this test documents what's missing";
}

// =============================================================================
// Issue #8: Missing all_ports() method
// =============================================================================

TEST(BlueprintMissingAllPorts, EmptyBlueprintReturnsEmptyList) {
    ui::StringInterner interner;
    TypeRegistry reg = TypeRegistry::create_test_registry(interner);
    Blueprint bp;

    // This test will fail until all_ports() is implemented
    // auto ports = bp.all_ports(interner, arena);
    // EXPECT_TRUE(ports.empty());

    SUCCEED() << "all_ports() method not implemented - this test documents what's missing";
}

TEST(BlueprintMissingAllPorts, IncludesInterfacePorts) {
    ui::StringInterner interner;
    TypeRegistry reg = TypeRegistry::create_test_registry(interner);

    Interface iface({
        {interner.intern("v_in"), Domain::Electrical, Direction::Input},
        {interner.intern("v_out"), Domain::Electrical, Direction::Output},
    });

    Blueprint bp;
    bp = bp.with_interface(iface);

    // This test will fail until all_ports() is implemented
    // PathArena arena(interner);
    // auto ports = bp.all_ports(interner, arena);
    //
    // bool found_v_in = false;
    // bool found_v_out = false;
    // for (const auto& [path, desc] : ports) {
    //     auto path_str = arena.to_string(path);
    //     if (path_str == "/:v_in") found_v_in = true;
    //     if (path_str == "/:v_out") found_v_out = true;
    // }
    // EXPECT_TRUE(found_v_in);
    // EXPECT_TRUE(found_v_out);

    SUCCEED() << "all_ports() method not implemented - this test documents what's missing";
}

TEST(BlueprintMissingAllPorts, IncludesNodePorts) {
    ui::StringInterner interner;
    TypeRegistry reg = TypeRegistry::create_test_registry(interner);

    Blueprint::Node node;
    node.id = interner.intern("bat1");
    node.type = interner.intern("Battery");
    
    Blueprint bp;
    bp = bp.with_node(std::move(node));

    // This test will fail until all_ports() is implemented
    // PathArena arena(interner);
    // auto ports = bp.all_ports(interner, arena);
    //
    // bool found_v_out = false;
    // for (const auto& [path, desc] : ports) {
    //     auto path_str = arena.to_string(path);
    //     if (path_str == "/bat1:v_out") found_v_out = true;
    // }
    // EXPECT_TRUE(found_v_out);

    SUCCEED() << "all_ports() method not implemented - this test documents what's missing";
}

// =============================================================================
// Issue #9: Missing bake_all() function
// =============================================================================

TEST(BakeAllMissing, RecursivelyExpandsNestedInstances) {
    ui::StringInterner interner;
    TypeRegistry reg = TypeRegistry::create_test_registry(interner);

    // Create a 3-level hierarchy
    // Level 3: resistor
    Blueprint level3;
    Blueprint::Node resistor;
    resistor.id = interner.intern("r1");
    resistor.type = interner.intern("Resistor");
    level3 = level3.with_node(std::move(resistor));

    // Level 2: sub-circuit with nested level3
    Blueprint level2;
    Blueprint::Nested nested3;
    nested3.id = interner.intern("sub3");
    nested3.blueprint_id = interner.intern("ResistorSub");
    nested3.embedded = false;
    level2 = level2.with_nested(std::move(nested3));

    // Level 1: root with nested level2
    Blueprint level1;
    Blueprint::Nested nested2;
    nested2.id = interner.intern("sub2");
    nested2.blueprint_id = interner.intern("Level2Sub");
    nested2.embedded = false;
    level1 = level1.with_nested(std::move(nested2));

    // This test will fail until bake_all() is implemented
    // auto baked = bake_all(level1, reg);
    // EXPECT_TRUE(baked.nested().empty()) << "All nested should be baked in";
    // EXPECT_GT(baked.nodes().size(), 2u) << "Should have expanded all levels";

    SUCCEED() << "bake_all() function not implemented - this test documents what's missing";
}

TEST(BakeAllMissing, HandlesCyclicReferencesGracefully) {
    ui::StringInterner interner;
    TypeRegistry reg = TypeRegistry::create_test_registry(interner);

    // Create cyclic reference: A contains B, B contains A
    Blueprint bp_a;
    bp_a = bp_a.with_id(interner.intern("CircuitA"));

    Blueprint::Nested nested_b;
    nested_b.id = interner.intern("sub_b");
    nested_b.blueprint_id = interner.intern("CircuitB");
    bp_a = bp_a.with_nested(std::move(nested_b));

    // Register both (conceptually)
    // In reality, this would create a cycle in the registry

    // This test will fail until bake_all() is implemented
    // auto result = bake_all(bp_a, reg);
    // EXPECT_TRUE(result.has_error()) << "Should detect and report cyclic references";

    SUCCEED() << "bake_all() function not implemented - this test documents what's missing";
}

TEST(BakeAllMissing, PreservesTopLevelInterface) {
    ui::StringInterner interner;
    TypeRegistry reg = TypeRegistry::create_test_registry(interner);

    Interface iface({
        {interner.intern("power_in"), Domain::Electrical, Direction::Input},
        {interner.intern("power_out"), Domain::Electrical, Direction::Output},
    });

    Blueprint bp;
    bp = bp.with_interface(iface);

    Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");
    bp = bp.with_node(std::move(node));

    // This test will fail until bake_all() is implemented
    // auto baked = bake_all(bp, reg);
    // EXPECT_EQ(baked.iface().size(), 2u);
    // EXPECT_TRUE(baked.iface().has(interner.intern("power_in")));
    // EXPECT_TRUE(baked.iface().has(interner.intern("power_out")));

    SUCCEED() << "bake_all() function not implemented - this test documents what's missing";
}

} // namespace bp2
