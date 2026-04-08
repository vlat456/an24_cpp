#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include <algorithm>

TEST(BlueprintNode, ConstructAndAccess) {
    ui::StringInterner interner;
    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("bat1");
    node.semantic.type = interner.intern("Battery");
    node.layout.x = 100.0f;
    node.layout.y = 200.0f;
    EXPECT_EQ(interner.resolve(node.semantic.id), "bat1");
    EXPECT_EQ(interner.resolve(node.semantic.type), "Battery");
    EXPECT_FLOAT_EQ(node.layout.x, 100.0f);
}

TEST(BlueprintWire, ConstructAndAccess) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto root = arena.root();
    auto src = arena.make_port(
        arena.make_node(root, interner.intern("b1")),
        interner.intern("v_out")
    );
    auto tgt = arena.make_port(
        arena.make_node(root, interner.intern("r1")),
        interner.intern("in")
    );

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = src;
    wire.target = tgt;
    wire.domain = Domain::Electrical;

    EXPECT_EQ(interner.resolve(wire.id), "w1");
    EXPECT_EQ(wire.source, src);
    EXPECT_EQ(wire.domain, Domain::Electrical);
}

TEST(BlueprintNested, ReferenceMode) {
    ui::StringInterner interner;
    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("power_system");
    nested.embedded = false;
    EXPECT_FALSE(nested.embedded);
    EXPECT_FALSE(nested.inline_def);
}

TEST(BlueprintNested, EmbeddedMode) {
    ui::StringInterner interner;
    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.embedded = true;
    nested.inline_def = std::make_unique<bp2::Blueprint>();
    EXPECT_TRUE(nested.embedded);
    EXPECT_TRUE(nested.inline_def != nullptr);
}

TEST(Blueprint, EmptyByDefault) {
    bp2::Blueprint bp;
    EXPECT_TRUE(bp.nodes().empty());
    EXPECT_TRUE(bp.wires().empty());
    EXPECT_TRUE(bp.nested().empty());
}

TEST(Blueprint, AddNodeAndFind) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("bat1");
    node.semantic.type = interner.intern("Battery");

    bp2::Blueprint bp2_val = bp.with_node(std::move(node));
    EXPECT_EQ(bp2_val.nodes().size(), 1u);

    auto* found = bp2_val.find_node(interner.intern("bat1"));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(interner.resolve(found->semantic.type), "Battery");
}

TEST(Blueprint, FindNodeNotFound) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    EXPECT_EQ(bp.find_node(interner.intern("nope")), nullptr);
}

TEST(Blueprint, WithNodeDoesNotMutateOriginal) {
    ui::StringInterner interner;
    bp2::Blueprint original;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("x");
    node.semantic.type = interner.intern("T");

    bp2::Blueprint modified = original.with_node(std::move(node));
    EXPECT_EQ(original.nodes().size(), 0u);
    EXPECT_EQ(modified.nodes().size(), 1u);
}

TEST(Blueprint, AddWireAndFind) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("a")),
        interner.intern("out")
    );
    wire.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("b")),
        interner.intern("in")
    );
    wire.domain = Domain::Electrical;

    bp2::Blueprint bp2_val = bp.with_wire(std::move(wire));
    EXPECT_EQ(bp2_val.wires().size(), 1u);

    auto* found = bp2_val.find_wire(interner.intern("w1"));
    ASSERT_NE(found, nullptr);
}

TEST(Blueprint, WithoutWire) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("a")),
        interner.intern("out")
    );
    wire.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("b")),
        interner.intern("in")
    );
    wire.domain = Domain::Electrical;

    bp = bp.with_wire(std::move(wire));
    EXPECT_EQ(bp.wires().size(), 1u);

    bp = bp.without_wire(interner.intern("w1"));
    EXPECT_EQ(bp.wires().size(), 0u);
}

TEST(Blueprint, AddNestedAndFind) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("power_system");
    nested.embedded = false;

    bp = bp.with_nested(std::move(nested));
    EXPECT_EQ(bp.nested().size(), 1u);

    auto* found = bp.find_nested(interner.intern("sub1"));
    ASSERT_NE(found, nullptr);
    EXPECT_FALSE(found->embedded);
}

TEST(Blueprint, WithoutNested) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("power_system");
    nested.embedded = false;

    bp = bp.with_nested(std::move(nested));
    bp = bp.without_nested(interner.intern("sub1"));
    EXPECT_EQ(bp.nested().size(), 0u);
}

TEST(Blueprint, WithId) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("my_bp"));
    EXPECT_EQ(interner.resolve(bp.id()), "my_bp");
}

TEST(Blueprint, WithDisplayName) {
    bp2::Blueprint bp;
    bp = bp.with_display_name("Power System");
    EXPECT_EQ(bp.display_name(), "Power System");
}

TEST(Blueprint, WithInterface) {
    ui::StringInterner interner;
    bp2::Interface iface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input}
    });
    bp2::Blueprint bp;
    bp = bp.with_interface(iface);
    EXPECT_EQ(bp.iface().size(), 1u);
}

TEST(Blueprint, EqualEmptyBlueprints) {
    bp2::Blueprint a, b;
    EXPECT_EQ(a, b);
}

TEST(Blueprint, UnequalById) {
    ui::StringInterner interner;
    auto bp_a = bp2::Blueprint().with_id(interner.intern("a"));
    auto bp_b = bp2::Blueprint().with_id(interner.intern("b"));
    EXPECT_NE(bp_a, bp_b);
}

TEST(Blueprint, UnequalByNodes) {
    ui::StringInterner interner;
    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.semantic.type = interner.intern("T");

    auto bp_a = bp2::Blueprint().with_node(node);
    auto bp_b = bp2::Blueprint();
    EXPECT_NE(bp_a, bp_b);
}

// ============================================================================
// Node sub-struct split: field isolation & equality
// ============================================================================

TEST(NodeSplit, SemanticFieldsAreIsolatedFromLayout) {
    ui::StringInterner interner;
    bp2::Blueprint::Node a, b;
    a.semantic.id = interner.intern("n1");
    a.semantic.type = interner.intern("Battery");
    a.layout.x = 100.0f;
    a.layout.y = 200.0f;

    b = a;
    b.layout.x = 999.0f;  // change layout only

    // Semantic sub-structs should still be equal
    EXPECT_EQ(a.semantic, b.semantic);
    // Layout sub-structs should differ
    EXPECT_NE(a.layout, b.layout);
    // Whole nodes should differ
    EXPECT_NE(a, b);
}

TEST(NodeSplit, ViewFieldsAreIsolatedFromSemantic) {
    ui::StringInterner interner;
    bp2::Blueprint::Node a, b;
    a.semantic.id = interner.intern("n1");
    a.semantic.type = interner.intern("Resistor");
    a.view.name = "My Resistor";
    a.view.render_hint = "default";

    b = a;
    b.view.name = "Renamed Resistor";

    EXPECT_EQ(a.semantic, b.semantic);
    EXPECT_EQ(a.layout, b.layout);
    EXPECT_NE(a.view, b.view);
    EXPECT_NE(a, b);
}

TEST(NodeSplit, EqualityRequiresAllThreeSubStructs) {
    ui::StringInterner interner;
    bp2::Blueprint::Node a, b;
    a.semantic.id = interner.intern("n1");
    a.semantic.type = interner.intern("Battery");
    a.layout.x = 10.0f;
    a.view.name = "bat";

    b = a;  // exact copy
    EXPECT_EQ(a, b);

    // Mutate each sub-struct and verify inequality
    auto c = a; c.semantic.params[interner.intern("r")] = 0.5f;
    EXPECT_NE(a, c);

    auto d = a; d.layout.collapsed = !a.layout.collapsed;
    EXPECT_NE(a, d);

    auto e = a; e.view.expandable = true;
    EXPECT_NE(a, e);
}

TEST(NodeSplit, PortListsLiveInViewData) {
    ui::StringInterner interner;
    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.view.inputs.emplace_back(interner.intern("v_in"), bp2::PortSide::Input, PortType::V);
    node.view.outputs.emplace_back(interner.intern("v_out"), bp2::PortSide::Output, PortType::V);

    // Verify ports are in view, not semantic or layout
    EXPECT_EQ(node.view.inputs.size(), 1u);
    EXPECT_EQ(node.view.outputs.size(), 1u);

    // A copy with different ports in view should differ
    auto other = node;
    other.view.inputs.clear();
    EXPECT_EQ(node.semantic, other.semantic);
    EXPECT_EQ(node.layout, other.layout);
    EXPECT_NE(node.view, other.view);
}
