#include <gtest/gtest.h>
#include "io/json/component_registry_json_loader.h"

#include "editor/visual/scene_mutations.h"
#include "editor/visual/scene.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/path/path.h"
#include "core/strings/interned_id.h"
#include "../bp2_test_helpers.h"

namespace {

/// Build a simple node with standard ports
static bp2::Blueprint::Node make_node(core::StringInterner& interner,
                                      const char* id,
                                      const char* type = "Battery") {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern(id);
    n.semantic.type = interner.intern(type);
    set_iface(n, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V)
    });
    return n;
}

/// Build a wire connecting two nodes
static bp2::Blueprint::Wire make_wire(core::StringInterner& interner,
                                      bp2::PathArena& /*arena*/,
                                      const char* wire_id,
                                      const char* src_node, const char* src_port,
                                      const char* dst_node, const char* dst_port) {
    bp2::Blueprint::Wire w;
    w.id = interner.intern(wire_id);
    w.source = bp2::WireEndpoint{interner.intern(src_node), interner.intern(src_port)};
    w.target = bp2::WireEndpoint{interner.intern(dst_node), interner.intern(dst_port)};
    return w;
}

} // namespace

/// Verify that a subwindow scene can be rebuilt from nested.inline_def directly,
/// without depending on root-level promoted shadow nodes/wires.
TEST(EmbeddedSubwindowScene, RebuildFromInlineDefIndependent) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    // Create inline blueprint (the "internals" of an embedded composite)
    bp2::Blueprint inline_bp;
    auto internal_n1 = make_node(interner, "inner_bat", "Battery");
    auto internal_n2 = make_node(interner, "inner_lamp", "Lamp");
    auto internal_wire = make_wire(interner, arena, "inner_wire_0",
                                   "inner_bat", "v_out", "inner_lamp", "v_in");

    inline_bp = inline_bp.with_node(std::move(internal_n1));
    inline_bp = inline_bp.with_node(std::move(internal_n2));
    inline_bp = inline_bp.with_wire(std::move(internal_wire));

    // Scene rebuild from inline_def should work without root shadow nodes
    visual::Scene scene;
    visual::mutations::rebuild(scene, inline_bp, interner, arena, std::span<const core::InternedId>{}, ComponentRegistry{});

    // Verify the internal nodes are rendered
    EXPECT_EQ(scene.roots().size(), 3u); // 2 nodes + 1 wire
    EXPECT_NE(scene.find("inner_bat"), nullptr);
    EXPECT_NE(scene.find("inner_lamp"), nullptr);
    EXPECT_NE(scene.find("inner_wire_0"), nullptr);
}

/// Verify that an embedded definition renders from its own document even when
/// another blueprint contains nodes with the same IDs.
TEST(EmbeddedSubwindowScene, InlineDefIndependentOfRootShadows) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    // Build inline definition
    bp2::Blueprint inline_bp;
    auto in1 = make_node(interner, "nested_bat", "Battery");
    auto in2 = make_node(interner, "nested_lamp", "Lamp");
    auto in_wire = make_wire(interner, arena, "nested_wire",
                             "nested_bat", "v_out", "nested_lamp", "v_in");
    inline_bp = inline_bp.with_node(std::move(in1));
    inline_bp = inline_bp.with_node(std::move(in2));
    inline_bp = inline_bp.with_wire(std::move(in_wire));

    // Create a separate root blueprint with duplicate IDs to prove the embedded
    // definition scene is independent from unrelated blueprint content.
    bp2::Blueprint root_bp;
    auto shadow_n1 = make_node(interner, "nested_bat", "Battery");
    auto shadow_n2 = make_node(interner, "nested_lamp", "Lamp");
    auto shadow_wire = make_wire(interner, arena, "nested_wire",
                                 "nested_bat", "v_out", "nested_lamp", "v_in");
    root_bp = root_bp.with_node(std::move(shadow_n1));
    root_bp = root_bp.with_node(std::move(shadow_n2));
    root_bp = root_bp.with_wire(std::move(shadow_wire));

    // Rebuild the scene directly from the embedded definition used by the subwindow.
    visual::Scene inline_scene;
    visual::mutations::rebuild(inline_scene, inline_bp, interner, arena, std::span<const core::InternedId>{}, ComponentRegistry{});
    EXPECT_EQ(inline_scene.roots().size(), 3u);

    // Rebuild the separate root blueprint to confirm it renders independently too.
    visual::Scene root_scene_filtered;
    visual::mutations::rebuild(root_scene_filtered, root_bp, interner, arena, std::span<const core::InternedId>{}, ComponentRegistry{});
    EXPECT_EQ(root_scene_filtered.roots().size(), 3u);

    // Both should have the same rendered content (nodes + wires)
    EXPECT_NE(inline_scene.find("nested_bat"), nullptr);
    EXPECT_NE(inline_scene.find("nested_lamp"), nullptr);
    EXPECT_NE(inline_scene.find("nested_wire"), nullptr);

    EXPECT_NE(root_scene_filtered.find("nested_bat"), nullptr);
    EXPECT_NE(root_scene_filtered.find("nested_lamp"), nullptr);
    EXPECT_NE(root_scene_filtered.find("nested_wire"), nullptr);
}

/// Verify that rebuilding the root window still renders root nodes alongside an
/// embedded blueprint instance node.
TEST(EmbeddedSubwindowScene, RootWindowStillShowsRootNodes) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    // Build a root blueprint with a root node and a blueprint-instance node.
    bp2::Blueprint bp;
    auto root_node = make_node(interner, "root_bat", "Battery");

    bp2::Blueprint inline_bp;
    inline_bp = inline_bp.with_node(make_node(interner, "composite_bat", "Battery"));

    bp2::Blueprint::Node group_node;
    group_node.semantic.id = interner.intern("composite_1");
    group_node.semantic.type = interner.intern("Composite");
    group_node.view.name = "composite_1";
    group_node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inline_bp.with_id(interner.intern("Composite"))))
    };

    auto wire = make_wire(interner, arena, "wire_root",
                          "root_bat", "v_out", "composite_1", "v_in");

    bp = bp.with_node(std::move(root_node));
    bp = bp.with_node(std::move(group_node));
    bp = bp.with_wire(std::move(wire));

    // Root rebuild renders the root blueprint, not the embedded child blueprint.
    visual::Scene root_scene;
    visual::mutations::rebuild(root_scene, bp, interner, arena, std::span<const core::InternedId>{}, ComponentRegistry{});

    EXPECT_EQ(root_scene.roots().size(), 2u);
    EXPECT_NE(root_scene.find("root_bat"), nullptr);
    EXPECT_NE(root_scene.find("composite_1"), nullptr);
    EXPECT_EQ(root_scene.find("composite_bat"), nullptr);

    // Embedded subwindow rebuild uses the inline child blueprint directly.
    visual::Scene sub_scene;
    visual::mutations::rebuild(sub_scene, inline_bp, interner, arena, std::span<const core::InternedId>{}, ComponentRegistry{});

    EXPECT_EQ(sub_scene.roots().size(), 1u);
    EXPECT_EQ(sub_scene.find("root_bat"), nullptr);
    EXPECT_EQ(sub_scene.find("composite_1"), nullptr);
    EXPECT_NE(sub_scene.find("composite_bat"), nullptr);
}

TEST(EmbeddedSubwindowScene, CompositeHostPortsUseNestedAuthorityNotCollapsedCache) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint inline_bp;
    inline_bp = inline_bp.with_interface(bp2::Interface({
        make_port(interner, "inner_only", Domain::Electrical, bp2::Direction::Input, PortType::V),
    }));

    // Create a blueprint-instance node with embedded source
    bp2::Blueprint::Node composite;
    composite.semantic.id = interner.intern("composite_1");
    composite.semantic.type = interner.intern("CompositeType");
    composite.view.name = "composite_1";

    // Attach the inline blueprint as source
    auto inline_bp_copy = std::make_unique<bp2::Blueprint>(inline_bp);
    composite.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inline_bp_copy->with_id(interner.intern("CompositeType"))))
    };

    bp2::Blueprint root;
    root = root.with_node(std::move(composite));

    visual::Scene root_scene;
    visual::mutations::rebuild(root_scene, root, interner, arena, std::span<const core::InternedId>{}, ComponentRegistry{});

    auto* composite_widget = root_scene.find("composite_1");
    ASSERT_NE(composite_widget, nullptr);
    EXPECT_NE(composite_widget->portByName("inner_only"), nullptr);

    auto* composite_node = root.find_node(interner.intern("composite_1"));
    ASSERT_NE(composite_node, nullptr);
    // With the sum-type design, a BlueprintInstance node's interface comes
    // exclusively from its source — no stale semantic.iface can exist.
    EXPECT_TRUE(composite_node->is_blueprint_instance());
    const auto effective = root.resolve_node_iface(*composite_node, bp2::Blueprint::NodeIfaceAuthority{interner});
    EXPECT_TRUE(effective.has(interner.intern("inner_only")));
}
