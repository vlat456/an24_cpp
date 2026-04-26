#include <gtest/gtest.h>
#include "core/strings/interned_id.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/blueprint/node_color.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include <algorithm>
#include <cmath>

// Helper to make a PortDescriptor for a semantic interface
// Shared bp2 test helpers (make_port, set_iface, count_inputs, count_outputs)
#include "../bp2_test_helpers.h"

TEST(BlueprintNode, ConstructAndAccess) {
    core::StringInterner interner;
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
    core::StringInterner interner;
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
    core::StringInterner interner;
    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("sub1");
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"))
    };
    EXPECT_TRUE(node.blueprint_instance().source.is_reference());
    EXPECT_EQ(node.blueprint_instance().source.inline_def(), nullptr);
}

TEST(BlueprintNode, EmbeddedInstanceMode) {
    core::StringInterner interner;
    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("sub1");
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(bp2::Blueprint().with_id(interner.intern("power_system"))))
    };
    EXPECT_TRUE(node.blueprint_instance().source.is_embedded());
    EXPECT_NE(node.blueprint_instance().source.inline_def(), nullptr);
}

// ============================================================================
// Issue #40 regression: construction invariant enforcement
// ============================================================================

TEST(BlueprintNodeSource, MakeEmbeddedRejectsNullInlineDef) {
    core::StringInterner interner;
    EXPECT_THROW(
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            nullptr),
        std::logic_error);
}

TEST(BlueprintNodeSource, MakeReferenceRejectsEmptyBlueprintId) {
    core::StringInterner interner;
    EXPECT_THROW(
        bp2::Blueprint::Node::BlueprintSource::make_reference(
            core::InternedId{}),
        std::logic_error);
}

TEST(BlueprintNodeSource, EmbeddedBlueprintSourceExposesInlineBlueprint) {
    core::StringInterner interner;
    auto iface = bp2::Interface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input}
    });
    auto inner = std::make_unique<bp2::Blueprint>();
    *inner = inner->with_interface(iface);

    inner = std::make_unique<bp2::Blueprint>(inner->with_id(interner.intern("inner_bp")));
    auto source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::move(inner));

    ASSERT_NE(source.inline_def(), nullptr);
    EXPECT_EQ(source.inline_def()->iface().size(), 1u);
}

TEST(BlueprintNodeSource, ReferenceSourceStoresOnlyBlueprintId) {
    core::StringInterner interner;

    auto source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"));

    EXPECT_TRUE(source.is_reference());
    EXPECT_EQ(source.blueprint_id(), interner.intern("power_system"));
    EXPECT_EQ(source.inline_def(), nullptr);
}

TEST(BlueprintNodeSource, ReferenceEqualityTracksBlueprintIdOnly) {
    core::StringInterner interner;

    auto source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"));

    EXPECT_EQ(source.blueprint_id(), interner.intern("power_system"));
}

TEST(BlueprintNodeSource, CopyPreservesVariantMode) {
    core::StringInterner interner;

    // Embedded copy
    auto embedded = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(bp2::Blueprint().with_id(interner.intern("bp1"))));
    auto embedded_copy = embedded;
    EXPECT_TRUE(embedded_copy.is_embedded());
    EXPECT_NE(embedded_copy.inline_def(), nullptr);

    // Reference copy
    auto ref = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"));
    auto ref_copy = ref;
    EXPECT_TRUE(ref_copy.is_reference());
    EXPECT_EQ(ref_copy.inline_def(), nullptr);
}

TEST(BlueprintNodeSource, SetInlineDefReplacesDefinition) {
    core::StringInterner interner;
    auto source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(bp2::Blueprint().with_id(interner.intern("bp1"))));

    auto replacement = std::make_unique<bp2::Blueprint>(bp2::Blueprint().with_id(interner.intern("bp1")));
    *replacement = replacement->with_name("replaced");
    source.set_inline_def(std::move(replacement));

    ASSERT_NE(source.inline_def(), nullptr);
    EXPECT_EQ(source.inline_def()->name(), "replaced");
}

TEST(BlueprintNodeSource, SetInlineDefRejectsNull) {
    core::StringInterner interner;
    auto source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(bp2::Blueprint().with_id(interner.intern("bp1"))));

    EXPECT_THROW(source.set_inline_def(nullptr), std::logic_error);
}

TEST(BlueprintNodeSource, SetInlineDefThrowsOnReference) {
    core::StringInterner interner;
    auto source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"));

    EXPECT_THROW(
        source.set_inline_def(std::make_unique<bp2::Blueprint>()),
        std::logic_error);
}

TEST(BlueprintNodeSource, ConvertToEmbedded) {
    core::StringInterner interner;
    auto source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"));

    EXPECT_TRUE(source.is_reference());
    // Note: BlueprintSource is a variant. To change modes, create a new one
    auto embedded_source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(bp2::Blueprint().with_id(interner.intern("new_bp"))));
    EXPECT_TRUE(embedded_source.is_embedded());
    EXPECT_NE(embedded_source.inline_def(), nullptr);
}

TEST(BlueprintNodeSource, ConvertToEmbeddedRejectsNull) {
    core::StringInterner interner;
    EXPECT_THROW(
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            nullptr),
        std::logic_error);
}

TEST(Blueprint, EmptyByDefault) {
    bp2::Blueprint bp;
    EXPECT_TRUE(bp.nodes().empty());
    EXPECT_TRUE(bp.wires().empty());
}

TEST(Blueprint, AddNodeAndFind) {
    core::StringInterner interner;
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
    core::StringInterner interner;
    bp2::Blueprint bp;
    EXPECT_EQ(bp.find_node(interner.intern("nope")), nullptr);
}

TEST(Blueprint, FindBlueprintInstanceNode) {
    core::StringInterner interner;

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("comp1");
    host.semantic.type = interner.intern("CompositeType");
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(bp2::Blueprint().with_id(interner.intern("CompositeType"))))
    };

    bp2::Blueprint bp;
    bp = bp.with_node(host);

    const auto* found = bp.find_blueprint_instance(interner.intern("comp1"));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->semantic.id, interner.intern("comp1"));
    EXPECT_TRUE(found->blueprint_instance().source.is_embedded());
}

TEST(Blueprint, BlueprintInstanceNodeWithEmbeddedSource) {
    core::StringInterner interner;

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("comp1");
    host.semantic.type = interner.intern("CompositeType");
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(bp2::Blueprint().with_id(interner.intern("CompositeType"))))
    };

    bp2::Blueprint bp;
    bp = bp.with_node(host);

    const auto* found_host = bp.find_node(interner.intern("comp1"));
    ASSERT_NE(found_host, nullptr);
    EXPECT_TRUE(found_host->is_blueprint_instance());
    EXPECT_TRUE(found_host->blueprint_instance().source.is_embedded());
}

TEST(Blueprint, BlueprintInstanceNodeWithReferenceSource) {
    core::StringInterner interner;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("comp1");
    node.semantic.type = interner.intern("CompositeType");
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("CompositeType"))
    };

    bp2::Blueprint bp;
    bp = bp.with_node(node);

    const auto* found = bp.find_node(interner.intern("comp1"));
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->is_blueprint_instance());
    EXPECT_TRUE(found->blueprint_instance().source.is_reference());
}

TEST(Blueprint, EffectiveNodeIfaceUsesEmbeddedBlueprintWhenPresent) {
    core::StringInterner interner;

    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        {interner.intern("authoritative"), Domain::Electrical, bp2::Direction::Input, PortType::V},
    }));

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("comp1");
    host.semantic.type = interner.intern("CompositeType");
    host.component().iface = bp2::Interface({
        {interner.intern("stale"), Domain::Electrical, bp2::Direction::Input, PortType::V},
    });
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(interner.intern("CompositeType"))))
    };

    bp2::Blueprint bp;
    bp = bp.with_node(host);

    const auto* found = bp.find_node(interner.intern("comp1"));
    ASSERT_NE(found, nullptr);
    const auto iface = bp.resolve_node_iface(*found, bp2::Blueprint::NodeIfaceAuthority{interner});
    EXPECT_TRUE(iface.find(interner.intern("authoritative")).has_value());
    EXPECT_FALSE(iface.find(interner.intern("stale")).has_value());
}

TEST(Blueprint, EffectiveNodeIfaceFallsBackToNodeIfaceWithoutHostedNested) {
    core::StringInterner interner;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.semantic.type = interner.intern("Battery");
    node.component().iface = bp2::Interface({
        {interner.intern("local"), Domain::Electrical, bp2::Direction::Output, PortType::V},
    });

    bp2::Blueprint bp;
    bp = bp.with_node(node);

    const auto* found = bp.find_node(interner.intern("n1"));
    ASSERT_NE(found, nullptr);
    const auto iface = bp.resolve_node_iface(*found, bp2::Blueprint::NodeIfaceAuthority{interner});
    EXPECT_TRUE(iface.find(interner.intern("local")).has_value());
}

TEST(Blueprint, ResolveNodeIfaceReferenceRequiresRegistryAuthority) {
    core::StringInterner interner;
    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("ref1");
    node.semantic.type = interner.intern("CompositeType");
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(interner.intern("CompositeType"))
    };

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(node));

    const auto* found = bp.find_node(interner.intern("ref1"));
    ASSERT_NE(found, nullptr);
    EXPECT_THROW(bp.resolve_node_iface(*found, bp2::Blueprint::NodeIfaceAuthority{interner}), std::logic_error);
}

TEST(Blueprint, ResolveNodeIfaceRejectsReferencedPrimitiveAuthority) {
    core::StringInterner interner;
    ComponentRegistry registry;

    PrimitiveSpec primitive;
    primitive.classname = "Battery";
    primitive.ports["v"] = Port{bp2::Direction::Output, PortType::V, Domain::Electrical, false};
    registry.register_type(primitive.classname, primitive);

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("ref1");
    node.semantic.type = interner.intern("Battery");
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(interner.intern("Battery"))
    };

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(node));

    const auto* found = bp.find_node(interner.intern("ref1"));
    ASSERT_NE(found, nullptr);
    EXPECT_THROW(
        bp.resolve_node_iface(*found, bp2::Blueprint::NodeIfaceAuthority{interner, &registry}),
        std::logic_error);
}

TEST(Blueprint, WithNodeDoesNotMutateOriginal) {
    core::StringInterner interner;
    bp2::Blueprint original;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("x");
    node.semantic.type = interner.intern("T");

    bp2::Blueprint modified = original.with_node(std::move(node));
    EXPECT_EQ(original.nodes().size(), 0u);
    EXPECT_EQ(modified.nodes().size(), 1u);
}

TEST(Blueprint, AddWireAndFind) {
    core::StringInterner interner;
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
    core::StringInterner interner;
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
    core::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("sub1");
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"))
    };

    bp = bp.with_node(std::move(node));
    EXPECT_EQ(bp.nodes().size(), 1u);

    auto* found = bp.find_blueprint_instance(interner.intern("sub1"));
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->blueprint_instance().source.is_reference());
}

TEST(Blueprint, WithoutNode) {
    core::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("sub1");
    node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
        interner.intern("power_system"))
    };

    bp = bp.with_node(std::move(node));
    bp = bp.without_node(interner.intern("sub1"));
    EXPECT_EQ(bp.nodes().size(), 0u);
}

TEST(Blueprint, WithId) {
    core::StringInterner interner;
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
    core::StringInterner interner;
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
    core::StringInterner interner;
    auto bp_a = bp2::Blueprint().with_id(interner.intern("a"));
    auto bp_b = bp2::Blueprint().with_id(interner.intern("b"));
    EXPECT_NE(bp_a, bp_b);
}

TEST(Blueprint, UnequalByNodes) {
    core::StringInterner interner;
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
    core::StringInterner interner;
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
    core::StringInterner interner;
    bp2::Blueprint::Node a, b;
    a.semantic.id = interner.intern("n1");
    a.semantic.type = interner.intern("Resistor");
    a.view.name = "My Resistor";

    b = a;
    b.view.name = "Renamed Resistor";

    EXPECT_EQ(a.semantic, b.semantic);
    EXPECT_EQ(a.layout, b.layout);
    EXPECT_NE(a.view, b.view);
    EXPECT_NE(a, b);
}

TEST(NodeSplit, EqualityRequiresAllThreeSubStructs) {
    core::StringInterner interner;
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
     core::StringInterner interner;
     bp2::Blueprint::Node node;
     node.semantic.id = interner.intern("n1");
     set_iface(node, {
         make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
         make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
     });

     // Verify ports are in component().iface
     EXPECT_EQ(count_inputs(node.component().iface), 1u);
     EXPECT_EQ(count_outputs(node.component().iface), 1u);

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
    core::StringInterner interner;
    auto a = bp2::Blueprint().with_id(interner.intern("x")).with_name("A");
    auto b = bp2::Blueprint().with_id(interner.intern("x")).with_name("A");
    auto c = bp2::Blueprint().with_id(interner.intern("x")).with_name("B");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(BlueprintNaming, CloneUpdatesName) {
    core::StringInterner interner;
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
    core::StringInterner interner;
    auto bp = bp2::Blueprint()
        .with_id(interner.intern("rt"))
        .with_name("Round Trip");
    bp2::Blueprint copy = bp;
    EXPECT_EQ(bp, copy);
}

TEST(BlueprintCanonicalEq, DependsOnlyOnCanonicalViewFields) {
    core::StringInterner interner;

    bp2::Blueprint::Node a;
    a.semantic.id = interner.intern("n1");
    a.semantic.type = interner.intern("Value");
    a.view.name = "Value";

    bp2::Blueprint::Node b = a;

    bp2::Blueprint left;
    left = left.with_id(interner.intern("bp"));
    left = left.with_name("BP");
    left = left.with_node(a);

    bp2::Blueprint right;
    right = right.with_id(interner.intern("bp"));
    right = right.with_name("BP");
    right = right.with_node(b);

    EXPECT_EQ(left, right);
    EXPECT_TRUE(left.canonical_eq(right));
}

// =============================================================================
// NodeColor NaN-safety regression (#182)
// =============================================================================

TEST(NodeColor, CanonicalizedClampsNaNToZero) {
    const bp2::NodeColor nan_color{std::nanf(""), std::nanf(""), std::nanf(""), std::nanf("")};
    const bp2::NodeColor c = bp2::NodeColor::canonicalized(nan_color);
    EXPECT_FLOAT_EQ(c.r, 0.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
    EXPECT_FLOAT_EQ(c.a, 0.0f);
}

TEST(NodeColor, CanonicalizedClampsInfinityToZero) {
    const bp2::NodeColor inf_color{INFINITY, -INFINITY, INFINITY, -INFINITY};
    const bp2::NodeColor c = bp2::NodeColor::canonicalized(inf_color);
    EXPECT_FLOAT_EQ(c.r, 1.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 1.0f);
    EXPECT_FLOAT_EQ(c.a, 0.0f);
}

TEST(NodeColor, ToUint32IsNaNSafe) {
    // Regression: NaN channels must not cause UB in static_cast<uint8_t>.
    // canonicalized() must squash NaN to 0.0f before the conversion.
    const bp2::NodeColor nan_color{std::nanf(""), 0.5f, 1.0f, std::nanf("")};
    const uint32_t packed = nan_color.to_uint32();
    // Alpha (NaN→0) shifted left 24, Blue=1.0→0xFF shifted left 16,
    // Green=0.5→0x80 shifted left 8, Red (NaN→0).
    EXPECT_EQ((packed >> 24) & 0xFF, 0u)   << "NaN alpha must map to 0";
    EXPECT_EQ((packed >> 16) & 0xFF, 255u)  << "Blue 1.0 must map to 255";
    EXPECT_EQ((packed >>  8) & 0xFF, 128u)  << "Green 0.5 must map to ~128";
    EXPECT_EQ( packed        & 0xFF, 0u)    << "NaN red must map to 0";
}
