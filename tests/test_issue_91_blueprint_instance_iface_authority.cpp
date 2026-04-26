#include <gtest/gtest.h>
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/validation/invariant_checker.h"
#include "core/strings/interned_id.h"
#include <nlohmann/json.hpp>
#include <unordered_set>

// Shared bp2 test helpers
#include "bp2_test_helpers.h"

namespace {

class Issue91BlueprintInstanceIfaceAuthorityTest : public ::testing::Test {
protected:
    core::StringInterner interner;
};

// Issue #91 Test 1: Embedded blueprint-instance interface derives from inline child blueprint only
TEST_F(Issue91BlueprintInstanceIfaceAuthorityTest, EmbeddedBlueprintInstanceDerivedFromInlineOnly) {
    // Create an inner blueprint with a specific interface
    bp2::Blueprint inner_bp;
    inner_bp = inner_bp.with_interface(bp2::Interface({
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "i_out", Domain::Electrical, bp2::Direction::Output, PortType::I),
    }));
    inner_bp = inner_bp.with_id(interner.intern("inner_type"));

    // Create a blueprint-instance node with embedded source
    bp2::Blueprint::Node bi_node;
    bi_node.semantic.id = interner.intern("nested_instance");
    bi_node.semantic.type = interner.intern("inner_type");
    bi_node.view.name = "nested_instance";
    // Issue #91: DO NOT set component().iface - it should be derived only
    bi_node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner_bp.with_id(interner.intern("inner_type"))))
    };

    bp2::Blueprint root;
    root = root.with_id(interner.intern("root"));
    root = root.with_node(std::move(bi_node));

    // Query authoritative interface via resolve_node_iface()
    const auto iface = root.resolve_node_iface(*root.find_node(interner.intern("nested_instance")), bp2::Blueprint::NodeIfaceAuthority{interner});
    
    // Verify interface matches the inline blueprint's interface
    ASSERT_EQ(iface.ports().size(), 2u);
    auto v_in = iface.find(interner.intern("v_in"));
    auto i_out = iface.find(interner.intern("i_out"));
    ASSERT_TRUE(v_in.has_value());
    ASSERT_TRUE(i_out.has_value());
    EXPECT_EQ(v_in->port_type, PortType::V);
    EXPECT_EQ(i_out->port_type, PortType::I);
}

// Issue #91 Test 2: Canonical save emits no host iface mirror for blueprint-instance
TEST_F(Issue91BlueprintInstanceIfaceAuthorityTest, CanonicalSaveEmitsNoIfaceMirrorForBlueprintInstance) {
    // Create an inner blueprint
    bp2::Blueprint inner_bp;
    inner_bp = inner_bp.with_interface(bp2::Interface({
        make_port(interner, "port1", Domain::Electrical, bp2::Direction::Input, PortType::V),
    }));
    inner_bp = inner_bp.with_id(interner.intern("inner"));

    // Create blueprint-instance node
    bp2::Blueprint::Node bi_node;
    bi_node.semantic.id = interner.intern("bi1");
    bi_node.semantic.type = interner.intern("inner");
    bi_node.view.name = "bi1";
    // Do NOT set component().iface
    bi_node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner_bp))
    };

    bp2::Blueprint root;
    root = root.with_id(interner.intern("root"));
    root = root.with_node(std::move(bi_node));

    // Encode to canonical JSON
    auto encoded_str = bp2::BlueprintCodec::encode(root, interner, bp2::PathArena(interner), nullptr);
    auto encoded = nlohmann::json::parse(encoded_str);
    
    // Check that blueprint-instance node has no "interface" or "iface" field persisted
    ASSERT_TRUE(encoded.contains("nodes"));
    ASSERT_TRUE(encoded["nodes"].is_array());
    ASSERT_EQ(encoded["nodes"].size(), 1u);
    
    const auto& node_json = encoded["nodes"][0];
    EXPECT_FALSE(node_json.contains("interface")) << "blueprint-instance node should not persist 'interface'";
    EXPECT_FALSE(node_json.contains("iface")) << "blueprint-instance node should not persist 'iface'";
    
    // Verify the node only has expected fields for blueprint-instance
    std::unordered_set<std::string> allowed_fields = {"id", "kind", "label", "source", "collapsed", "layout"};
    for (auto& [key, value] : node_json.items()) {
        EXPECT_TRUE(allowed_fields.count(key) > 0) 
            << "Unexpected field '" << key << "' in persisted blueprint-instance node";
    }
}

// Issue #91 Test 3: effective_node_iface returns source authority for blueprint-instance
TEST_F(Issue91BlueprintInstanceIfaceAuthorityTest, EffectiveNodeIfaceReturnsSourceAuthority) {
    // Create an inner blueprint with explicit interface
    bp2::Blueprint inner_bp;
    auto inner_iface = bp2::Interface({
        make_port(interner, "a", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "b", Domain::Electrical, bp2::Direction::Output, PortType::I),
    });
    inner_bp = inner_bp.with_interface(inner_iface);
    inner_bp = inner_bp.with_id(interner.intern("inner"));

    // Create blueprint-instance node without setting component().iface
    bp2::Blueprint::Node bi_node;
    bi_node.semantic.id = interner.intern("bi");
    bi_node.semantic.type = interner.intern("inner");
    bi_node.view.name = "bi";
    // Issue #91: Deliberately leave component().iface empty
    EXPECT_EQ(bi_node.component().iface.ports().size(), 0u);
    
    bi_node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner_bp.with_id(interner.intern("inner"))))
    };

    bp2::Blueprint root;
    root = root.with_id(interner.intern("root"));
    root = root.with_node(std::move(bi_node));

    // Verify effective_node_iface returns the source authority, NOT the (empty) component().iface
    const auto* bi = root.find_node(interner.intern("bi"));
    const auto effective = root.resolve_node_iface(*bi, bp2::Blueprint::NodeIfaceAuthority{interner});
    
    // Should match inner_iface, not the empty component().iface
    ASSERT_EQ(effective.ports().size(), 2u);
    auto a = effective.find(interner.intern("a"));
    auto b = effective.find(interner.intern("b"));
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(a->port_type, PortType::V);
    EXPECT_EQ(b->port_type, PortType::I);
}

// Issue #91 Test 4: Component nodes still use component().iface (unchanged)
TEST_F(Issue91BlueprintInstanceIfaceAuthorityTest, ComponentNodesStillUseSematicIface) {
    // Create a component node with explicit component().iface
    bp2::Blueprint::Node comp_node;
    comp_node.content = bp2::Blueprint::Node::ComponentData{};
    comp_node.semantic.id = interner.intern("comp1");
    comp_node.semantic.type = interner.intern("Battery");
    comp_node.view.name = "comp1";
    comp_node.component().iface = bp2::Interface({
        make_port(interner, "v", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint root;
    root = root.with_id(interner.intern("root"));
    root = root.with_node(std::move(comp_node));

    // effective_node_iface should return component().iface for component nodes
    const auto* comp = root.find_node(interner.intern("comp1"));
    const auto effective = root.resolve_node_iface(*comp, bp2::Blueprint::NodeIfaceAuthority{interner});
    
    ASSERT_EQ(effective.ports().size(), 1u);
    auto v = effective.find(interner.intern("v"));
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->port_type, PortType::V);
}

} // namespace
