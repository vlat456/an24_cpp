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
    bp2::WireEndpoint src{interner.intern("b1"), interner.intern("v_out")};
    bp2::WireEndpoint tgt{interner.intern("r1"), interner.intern("in")};

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = src;
    wire.target = tgt;
    wire.domain = Domain::Electrical;

    EXPECT_EQ(interner.resolve(wire.id), "w1");
    EXPECT_EQ(wire.source, src);
    EXPECT_EQ(wire.domain, Domain::Electrical);
}

TEST(BlueprintNode, ReferenceInstanceMode) {
    ui::StringInterner interner;
    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("sub1");
    node.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    node.source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"),
        bp2::Interface{});
    EXPECT_TRUE(node.source->is_reference());
    EXPECT_EQ(node.source->inline_def(), nullptr);
}

TEST(BlueprintNode, EmbeddedInstanceMode) {
    ui::StringInterner interner;
    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("sub1");
    node.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    node.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("power_system"),
        std::make_unique<bp2::Blueprint>());
    EXPECT_TRUE(node.source->is_embedded());
    EXPECT_NE(node.source->inline_def(), nullptr);
}

// ============================================================================
// Issue #40 regression: construction invariant enforcement
// ============================================================================

TEST(BlueprintNodeSource, MakeEmbeddedRejectsNullInlineDef) {
    ui::StringInterner interner;
    EXPECT_THROW(
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            interner.intern("bp_type"),
            nullptr),
        std::logic_error);
}

TEST(BlueprintNodeSource, MakeReferenceRejectsEmptyBlueprintId) {
    ui::StringInterner interner;
    EXPECT_THROW(
        bp2::Blueprint::Node::BlueprintSource::make_reference(
            ui::InternedId{},  // empty
            bp2::Interface{}),
        std::logic_error);
}

TEST(BlueprintNodeSource, CachedIfaceNonThrowingEmbedded) {
    ui::StringInterner interner;
    auto iface = bp2::Interface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input}
    });
    auto inner = std::make_unique<bp2::Blueprint>();
    *inner = inner->with_interface(iface);

    auto source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("inner_bp"),
        std::move(inner));

    EXPECT_NO_THROW(source.cached_iface());
    EXPECT_EQ(source.cached_iface().size(), 1u);
}

TEST(BlueprintNodeSource, CachedIfaceNonThrowingReference) {
    ui::StringInterner interner;
    auto iface = bp2::Interface({
        {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output}
    });

    auto source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"),
        iface);

    EXPECT_NO_THROW(source.cached_iface());
    EXPECT_EQ(source.cached_iface().size(), 1u);
}

TEST(BlueprintNodeSource, CachedIfaceTracksReferenceCacheOnly) {
    ui::StringInterner interner;
    auto iface = bp2::Interface({
        {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output}
    });

    auto source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"),
        iface);

    EXPECT_EQ(source.blueprint_id(), interner.intern("power_system"));
    EXPECT_EQ(source.cached_iface(), iface);
}

TEST(BlueprintNodeSource, CopyPreservesVariantMode) {
    ui::StringInterner interner;

    // Embedded copy
    auto embedded = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("bp1"),
        std::make_unique<bp2::Blueprint>());
    auto embedded_copy = embedded;
    EXPECT_TRUE(embedded_copy.is_embedded());
    EXPECT_NE(embedded_copy.inline_def(), nullptr);

    // Reference copy
    auto ref = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"),
        bp2::Interface{});
    auto ref_copy = ref;
    EXPECT_TRUE(ref_copy.is_reference());
    EXPECT_EQ(ref_copy.inline_def(), nullptr);
}

TEST(BlueprintNodeSource, SetInlineDefReplacesDefinition) {
    ui::StringInterner interner;
    auto source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("bp1"),
        std::make_unique<bp2::Blueprint>());

    auto replacement = std::make_unique<bp2::Blueprint>();
    *replacement = replacement->with_name("replaced");
    source.set_inline_def(std::move(replacement));

    ASSERT_NE(source.inline_def(), nullptr);
    EXPECT_EQ(source.inline_def()->name(), "replaced");
}

TEST(BlueprintNodeSource, SetInlineDefRejectsNull) {
    ui::StringInterner interner;
    auto source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("bp1"),
        std::make_unique<bp2::Blueprint>());

    EXPECT_THROW(source.set_inline_def(nullptr), std::logic_error);
}

TEST(BlueprintNodeSource, SetInlineDefThrowsOnReference) {
    ui::StringInterner interner;
    auto source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"),
        bp2::Interface{});

    EXPECT_THROW(
        source.set_inline_def(std::make_unique<bp2::Blueprint>()),
        std::logic_error);
}

TEST(BlueprintNodeSource, ConvertToEmbedded) {
    ui::StringInterner interner;
    auto source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"),
        bp2::Interface{});

    EXPECT_TRUE(source.is_reference());
    // Note: BlueprintSource is a variant. To change modes, create a new one
    auto embedded_source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("new_bp"),
        std::make_unique<bp2::Blueprint>());
    EXPECT_TRUE(embedded_source.is_embedded());
    EXPECT_NE(embedded_source.inline_def(), nullptr);
}

TEST(BlueprintNodeSource, ConvertToEmbeddedRejectsNull) {
    ui::StringInterner interner;
    EXPECT_THROW(
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            interner.intern("bp"), nullptr),
        std::logic_error);
}

TEST(Blueprint, EmptyByDefault) {
    bp2::Blueprint bp;
    EXPECT_TRUE(bp.nodes().empty());
    EXPECT_TRUE(bp.wires().empty());
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

TEST(Blueprint, FindBlueprintInstanceNode) {
    ui::StringInterner interner;

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("comp1");
    host.semantic.type = interner.intern("CompositeType");
    host.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    host.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("CompositeType"), std::make_unique<bp2::Blueprint>());

    bp2::Blueprint bp;
    bp = bp.with_node(host);

    const auto* found = bp.find_blueprint_instance(interner.intern("comp1"));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->semantic.id, interner.intern("comp1"));
    EXPECT_TRUE(found->source->is_embedded());
}

TEST(Blueprint, BlueprintInstanceNodeWithEmbeddedSource) {
    ui::StringInterner interner;

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("comp1");
    host.semantic.type = interner.intern("CompositeType");
    host.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    host.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("CompositeType"), std::make_unique<bp2::Blueprint>());

    bp2::Blueprint bp;
    bp = bp.with_node(host);

    const auto* found_host = bp.find_node(interner.intern("comp1"));
    ASSERT_NE(found_host, nullptr);
    EXPECT_TRUE(found_host->kind == bp2::Blueprint::Node::Kind::BlueprintInstance);
    EXPECT_TRUE(found_host->source->is_embedded());
}

TEST(Blueprint, BlueprintInstanceNodeWithReferenceSource) {
    ui::StringInterner interner;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("comp1");
    node.semantic.type = interner.intern("CompositeType");
    node.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    node.source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("CompositeType"), bp2::Interface{});

    bp2::Blueprint bp;
    bp = bp.with_node(node);

    const auto* found = bp.find_node(interner.intern("comp1"));
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->kind == bp2::Blueprint::Node::Kind::BlueprintInstance);
    EXPECT_TRUE(found->source->is_reference());
}

TEST(Blueprint, EffectiveNodeIfaceUsesEmbeddedBlueprintWhenPresent) {
    ui::StringInterner interner;

    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        {interner.intern("authoritative"), Domain::Electrical, bp2::Direction::Input, PortType::V},
    }));

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("comp1");
    host.semantic.type = interner.intern("CompositeType");
    host.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    host.semantic.iface = bp2::Interface({
        {interner.intern("stale"), Domain::Electrical, bp2::Direction::Input, PortType::V},
    });
    host.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("CompositeType"), std::make_unique<bp2::Blueprint>(inner));

    bp2::Blueprint bp;
    bp = bp.with_node(host);

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
    bp2::Blueprint bp;

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = bp2::WireEndpoint{interner.intern("a"), interner.intern("out")};
    wire.target = bp2::WireEndpoint{interner.intern("b"), interner.intern("in")};
    wire.domain = Domain::Electrical;

    bp2::Blueprint bp2_val = bp.with_wire(std::move(wire));
    EXPECT_EQ(bp2_val.wires().size(), 1u);

    auto* found = bp2_val.find_wire(interner.intern("w1"));
    ASSERT_NE(found, nullptr);
}

TEST(Blueprint, WithoutWire) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = bp2::WireEndpoint{interner.intern("a"), interner.intern("out")};
    wire.target = bp2::WireEndpoint{interner.intern("b"), interner.intern("in")};
    wire.domain = Domain::Electrical;

    bp = bp.with_wire(std::move(wire));
    EXPECT_EQ(bp.wires().size(), 1u);

    bp = bp.without_wire(interner.intern("w1"));
    EXPECT_EQ(bp.wires().size(), 0u);
}

TEST(Blueprint, AddBlueprintInstanceNodeAndFind) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("sub1");
    node.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    node.source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"),
        bp2::Interface{});

    bp = bp.with_node(std::move(node));
    EXPECT_EQ(bp.nodes().size(), 1u);

    auto* found = bp.find_blueprint_instance(interner.intern("sub1"));
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->source->is_reference());
}

TEST(Blueprint, WithoutNode) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("sub1");
    node.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    node.source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"),
        bp2::Interface{});

    bp = bp.with_node(std::move(node));
    bp = bp.without_node(interner.intern("sub1"));
    EXPECT_EQ(bp.nodes().size(), 0u);
}

TEST(Blueprint, WithId) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("my_bp"));
    EXPECT_EQ(interner.resolve(bp.id()), "my_bp");
}

TEST(Blueprint, WithName) {
    bp2::Blueprint bp;
    bp = bp.with_name("Power System");
    EXPECT_EQ(bp.name(), "Power System");
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

    auto e = a; e.view.name = "different";
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

// ============================================================================
// Regression: single-name model (display_name/name duality removed)
// ============================================================================

TEST(BlueprintNaming, SingleNameField_WithNameSetsName) {
    bp2::Blueprint bp;
    bp = bp.with_name("My Blueprint");
    EXPECT_EQ(bp.name(), "My Blueprint");
}

TEST(BlueprintNaming, EqualityUsesName) {
    ui::StringInterner interner;
    auto a = bp2::Blueprint().with_id(interner.intern("x")).with_name("A");
    auto b = bp2::Blueprint().with_id(interner.intern("x")).with_name("A");
    auto c = bp2::Blueprint().with_id(interner.intern("x")).with_name("B");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(BlueprintNaming, CloneUpdatesName) {
    ui::StringInterner interner;
    auto bp = bp2::Blueprint()
        .with_id(interner.intern("orig"))
        .with_name("Original");
    auto cloned = bp.clone(interner.intern("copy"));
    EXPECT_EQ(cloned.name(), "Copy of Original");
    EXPECT_EQ(cloned.id(), interner.intern("copy"));
}

TEST(BlueprintNaming, RoundTripEquality) {
    // Verifies that a Blueprint constructed with with_name() compares equal
    // to itself after a copy — no hidden fields can cause divergence.
    ui::StringInterner interner;
    auto bp = bp2::Blueprint()
        .with_id(interner.intern("rt"))
        .with_name("Round Trip");
    bp2::Blueprint copy = bp;
    EXPECT_EQ(bp, copy);
}

TEST(BlueprintCanonicalEq, IgnoresHydratedAndSessionOnlyViewFields) {
    ui::StringInterner interner;

    bp2::Blueprint::Node a;
    a.semantic.id = interner.intern("n1");
    a.semantic.type = interner.intern("Value");
    a.view.name = "Value";
    a.view.render_hint = "ref";
    a.view.content_type = bp2::NodeContentType::Gauge;
    a.view.content_label = "Volts";
    a.view.has_color = true;
    a.view.color_r = 1.0f;

    bp2::Blueprint::Node b = a;
    b.view.render_hint.clear();
    b.view.content_type = bp2::NodeContentType::None;
    b.view.content_label.clear();
    b.view.has_color = false;
    b.view.color_r = 0.5f;

    bp2::Blueprint left;
    left = left.with_id(interner.intern("bp"));
    left = left.with_name("BP");
    left = left.with_node(a);

    bp2::Blueprint right;
    right = right.with_id(interner.intern("bp"));
    right = right.with_name("BP");
    right = right.with_node(b);

    EXPECT_NE(left, right);
    EXPECT_TRUE(left.canonical_eq(right));
}
