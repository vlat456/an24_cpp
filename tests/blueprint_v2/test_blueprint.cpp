#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include <algorithm>

// Helper to make a PortDescriptor for a semantic interface
// Shared bp2 test helpers (make_port, set_iface, count_inputs, count_outputs)
#include "../bp2_test_helpers.h"

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
    auto nested = bp2::Blueprint::Nested::make_reference(
        interner.intern("sub1"),
        interner.intern("power_system"),
        bp2::Interface{});
    EXPECT_TRUE(nested.is_reference());
    EXPECT_EQ(nested.inline_def(), nullptr);
}

TEST(BlueprintNested, EmbeddedMode) {
    ui::StringInterner interner;
    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("sub1"),
        ui::InternedId{},
        std::make_unique<bp2::Blueprint>());
    EXPECT_TRUE(nested.is_embedded());
    EXPECT_NE(nested.inline_def(), nullptr);
}

// ============================================================================
// Issue #40 regression: construction invariant enforcement
// ============================================================================

TEST(BlueprintNested, MakeEmbeddedRejectsNullInlineDef) {
    ui::StringInterner interner;
    EXPECT_THROW(
        bp2::Blueprint::Nested::make_embedded(
            interner.intern("sub1"),
            interner.intern("bp_type"),
            nullptr),
        std::logic_error);
}

TEST(BlueprintNested, MakeReferenceRejectsEmptyBlueprintId) {
    ui::StringInterner interner;
    EXPECT_THROW(
        bp2::Blueprint::Nested::make_reference(
            interner.intern("sub1"),
            ui::InternedId{},  // empty
            bp2::Interface{}),
        std::logic_error);
}

TEST(BlueprintNested, ResolvedIfaceNonThrowingEmbedded) {
    ui::StringInterner interner;
    auto iface = bp2::Interface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input}
    });
    auto inner = std::make_unique<bp2::Blueprint>();
    *inner = inner->with_interface(iface);

    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("sub1"),
        interner.intern("inner_bp"),
        std::move(inner));

    EXPECT_NO_THROW(nested.resolved_iface());
    EXPECT_EQ(nested.resolved_iface().size(), 1u);
}

TEST(BlueprintNested, ResolvedIfaceNonThrowingReference) {
    ui::StringInterner interner;
    auto iface = bp2::Interface({
        {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output}
    });

    auto nested = bp2::Blueprint::Nested::make_reference(
        interner.intern("sub1"),
        interner.intern("power_system"),
        iface);

    EXPECT_NO_THROW(nested.resolved_iface());
    EXPECT_EQ(nested.resolved_iface().size(), 1u);
}

TEST(BlueprintNested, CopyPreservesVariantMode) {
    ui::StringInterner interner;

    // Embedded copy
    auto embedded = bp2::Blueprint::Nested::make_embedded(
        interner.intern("e1"),
        interner.intern("bp1"),
        std::make_unique<bp2::Blueprint>());
    bp2::Blueprint::Nested embedded_copy = embedded;
    EXPECT_TRUE(embedded_copy.is_embedded());
    EXPECT_NE(embedded_copy.inline_def(), nullptr);

    // Reference copy
    auto ref = bp2::Blueprint::Nested::make_reference(
        interner.intern("r1"),
        interner.intern("power_system"),
        bp2::Interface{});
    bp2::Blueprint::Nested ref_copy = ref;
    EXPECT_TRUE(ref_copy.is_reference());
    EXPECT_EQ(ref_copy.inline_def(), nullptr);
}

TEST(BlueprintNested, SetInlineDefReplacesDefinition) {
    ui::StringInterner interner;
    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("e1"),
        interner.intern("bp1"),
        std::make_unique<bp2::Blueprint>());

    auto replacement = std::make_unique<bp2::Blueprint>();
    *replacement = replacement->with_display_name("replaced");
    nested.set_inline_def(std::move(replacement));

    ASSERT_NE(nested.inline_def(), nullptr);
    EXPECT_EQ(nested.inline_def()->display_name(), "replaced");
}

TEST(BlueprintNested, SetInlineDefRejectsNull) {
    ui::StringInterner interner;
    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("e1"),
        interner.intern("bp1"),
        std::make_unique<bp2::Blueprint>());

    EXPECT_THROW(nested.set_inline_def(nullptr), std::logic_error);
}

TEST(BlueprintNested, SetInlineDefThrowsOnReference) {
    ui::StringInterner interner;
    auto nested = bp2::Blueprint::Nested::make_reference(
        interner.intern("r1"),
        interner.intern("power_system"),
        bp2::Interface{});

    EXPECT_THROW(
        nested.set_inline_def(std::make_unique<bp2::Blueprint>()),
        std::bad_variant_access);
}

TEST(BlueprintNested, ConvertToEmbedded) {
    ui::StringInterner interner;
    auto nested = bp2::Blueprint::Nested::make_reference(
        interner.intern("r1"),
        interner.intern("power_system"),
        bp2::Interface{});

    EXPECT_TRUE(nested.is_reference());
    nested.convert_to_embedded(
        interner.intern("new_bp"),
        std::make_unique<bp2::Blueprint>());
    EXPECT_TRUE(nested.is_embedded());
    EXPECT_NE(nested.inline_def(), nullptr);
}

TEST(BlueprintNested, ConvertToEmbeddedRejectsNull) {
    ui::StringInterner interner;
    auto nested = bp2::Blueprint::Nested::make_reference(
        interner.intern("r1"),
        interner.intern("power_system"),
        bp2::Interface{});

    EXPECT_THROW(
        nested.convert_to_embedded(interner.intern("bp"), nullptr),
        std::logic_error);
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

TEST(Blueprint, FindHostedNestedForCompositeHost) {
    ui::StringInterner interner;

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("comp1");
    host.semantic.type = interner.intern("CompositeType");
    host.view.expandable = true;

    bp2::Blueprint bp;
    bp = bp.with_node(host);
    bp = bp.with_nested(bp2::Blueprint::Nested::make_embedded(
        interner.intern("comp1"), interner.intern("CompositeType"), std::make_unique<bp2::Blueprint>()));

    const auto* found_host = bp.find_node(interner.intern("comp1"));
    ASSERT_NE(found_host, nullptr);
    const auto* nested = bp.find_hosted_nested(*found_host);
    ASSERT_NE(nested, nullptr);
    EXPECT_EQ(nested->id, interner.intern("comp1"));
}

TEST(Blueprint, EmbeddedProxyNodeDetectionRequiresEmbeddedNestedAndExpandable) {
    ui::StringInterner interner;

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("comp1");
    host.semantic.type = interner.intern("CompositeType");
    host.view.expandable = true;

    bp2::Blueprint bp;
    bp = bp.with_node(host);
    bp = bp.with_nested(bp2::Blueprint::Nested::make_embedded(
        interner.intern("comp1"), interner.intern("CompositeType"), std::make_unique<bp2::Blueprint>()));

    const auto* found_host = bp.find_node(interner.intern("comp1"));
    ASSERT_NE(found_host, nullptr);
    EXPECT_TRUE(bp.is_embedded_proxy_node(*found_host));
}

TEST(Blueprint, EmbeddedProxyNodeDetectionDoesNotTriggerForSameIdNonExpandableNode) {
    ui::StringInterner interner;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("comp1");
    node.semantic.type = interner.intern("OrdinaryType");
    node.view.expandable = false;

    bp2::Blueprint bp;
    bp = bp.with_node(node);
    bp = bp.with_nested(bp2::Blueprint::Nested::make_embedded(
        interner.intern("comp1"), interner.intern("CompositeType"), std::make_unique<bp2::Blueprint>()));

    const auto* found = bp.find_node(interner.intern("comp1"));
    ASSERT_NE(found, nullptr);
    EXPECT_FALSE(bp.is_embedded_proxy_node(*found));
}

TEST(Blueprint, EmbeddedProxyNodeDetectionDoesNotTriggerForReferenceNested) {
    ui::StringInterner interner;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("comp1");
    node.semantic.type = interner.intern("ReferenceType");
    node.view.expandable = true;

    bp2::Blueprint bp;
    bp = bp.with_node(node);
    bp = bp.with_nested(bp2::Blueprint::Nested::make_reference(
        interner.intern("comp1"), interner.intern("ReferenceType"), bp2::Interface{}));

    const auto* found = bp.find_node(interner.intern("comp1"));
    ASSERT_NE(found, nullptr);
    EXPECT_FALSE(bp.is_embedded_proxy_node(*found));
}

TEST(Blueprint, EffectiveNodeIfaceUsesHostedNestedAuthorityWhenPresent) {
    ui::StringInterner interner;

    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        {interner.intern("authoritative"), Domain::Electrical, bp2::Direction::Input, PortType::V},
    }));

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("comp1");
    host.semantic.type = interner.intern("CompositeType");
    host.view.expandable = true;
    host.semantic.iface = bp2::Interface({
        {interner.intern("stale"), Domain::Electrical, bp2::Direction::Input, PortType::V},
    });

    bp2::Blueprint bp;
    bp = bp.with_node(host);
    bp = bp.with_nested(bp2::Blueprint::Nested::make_embedded(
        interner.intern("comp1"), interner.intern("CompositeType"), std::make_unique<bp2::Blueprint>(inner)));

    const auto* found = bp.find_node(interner.intern("comp1"));
    ASSERT_NE(found, nullptr);
    const auto& iface = bp.effective_node_iface(*found);
    EXPECT_TRUE(iface.find(interner.intern("authoritative")).has_value());
    EXPECT_FALSE(iface.find(interner.intern("stale")).has_value());
}

TEST(Blueprint, EffectiveNodeIfaceFallsBackToNodeIfaceWithoutHostedNested) {
    ui::StringInterner interner;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.semantic.type = interner.intern("Battery");
    node.semantic.iface = bp2::Interface({
        {interner.intern("local"), Domain::Electrical, bp2::Direction::Output, PortType::V},
    });

    bp2::Blueprint bp;
    bp = bp.with_node(node);

    const auto* found = bp.find_node(interner.intern("n1"));
    ASSERT_NE(found, nullptr);
    const auto& iface = bp.effective_node_iface(*found);
    EXPECT_TRUE(iface.find(interner.intern("local")).has_value());
}

TEST(Blueprint, EffectiveNodeIfaceThrowsForMissingNodeId) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    EXPECT_THROW(bp.effective_node_iface(interner.intern("missing")), std::logic_error);
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

    auto nested = bp2::Blueprint::Nested::make_reference(
        interner.intern("sub1"),
        interner.intern("power_system"),
        bp2::Interface{});

    bp = bp.with_nested(std::move(nested));
    EXPECT_EQ(bp.nested().size(), 1u);

    auto* found = bp.find_nested(interner.intern("sub1"));
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->is_reference());
}

TEST(Blueprint, WithoutNested) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    auto nested = bp2::Blueprint::Nested::make_reference(
        interner.intern("sub1"),
        interner.intern("power_system"),
        bp2::Interface{});

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

TEST(NodeSplit, PortListsLiveInSemanticIface) {
     ui::StringInterner interner;
     bp2::Blueprint::Node node;
     node.semantic.id = interner.intern("n1");
     set_iface(node, {
         make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
         make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
     });

     // Verify ports are in semantic.iface
     EXPECT_EQ(count_inputs(node.semantic.iface), 1u);
     EXPECT_EQ(count_outputs(node.semantic.iface), 1u);

     // A copy with different ports should differ
     auto other = node;
     EXPECT_EQ(node.semantic, other.semantic);
     EXPECT_EQ(node.layout, other.layout);
     EXPECT_EQ(node.view, other.view);
 }
