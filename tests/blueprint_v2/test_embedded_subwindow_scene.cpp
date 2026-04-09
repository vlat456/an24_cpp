#include <gtest/gtest.h>

#include "editor/visual/scene_mutations.h"
#include "editor/visual/scene.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include "../bp2_test_helpers.h"

namespace {

/// Build a simple node with standard ports
static bp2::Blueprint::Node make_node(ui::StringInterner& interner,
                                      const char* id,
                                      const char* type = "Battery",
                                      const char* layout_group = "") {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern(id);
    n.semantic.type = interner.intern(type);
    n.layout.layout_group = layout_group;
    set_iface(n, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V)
    });
    return n;
}

/// Build a wire connecting two nodes
static bp2::Blueprint::Wire make_wire(ui::StringInterner& interner,
                                      bp2::PathArena& arena,
                                      const char* wire_id,
                                      const char* src_node, const char* src_port,
                                      const char* dst_node, const char* dst_port) {
    bp2::Blueprint::Wire w;
    w.id = interner.intern(wire_id);
    w.source = arena.make_port(arena.make_node(arena.root(), interner.intern(src_node)),
                               interner.intern(src_port));
    w.target = arena.make_port(arena.make_node(arena.root(), interner.intern(dst_node)),
                               interner.intern(dst_port));
    return w;
}

} // namespace

/// Verify that a subwindow scene can be rebuilt from nested.inline_def directly,
/// without depending on root-level promoted shadow nodes/wires.
TEST(EmbeddedSubwindowScene, RebuildFromInlineDefIndependent) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    // Create inline blueprint (the "internals" of an embedded composite)
    bp2::Blueprint inline_bp;
    auto internal_n1 = make_node(interner, "inner_bat", "Battery", "");
    auto internal_n2 = make_node(interner, "inner_lamp", "Lamp", "");
    auto internal_wire = make_wire(interner, arena, "inner_wire_0",
                                   "inner_bat", "v_out", "inner_lamp", "v_in");

    inline_bp = inline_bp.with_node(std::move(internal_n1));
    inline_bp = inline_bp.with_node(std::move(internal_n2));
    inline_bp = inline_bp.with_wire(std::move(internal_wire));

    // Scene rebuild from inline_def should work without root shadow nodes
    visual::Scene scene;
    visual::mutations::rebuild(scene, inline_bp, interner, arena, "");

    // Verify the internal nodes are rendered
    EXPECT_EQ(scene.roots().size(), 3u); // 2 nodes + 1 wire
    EXPECT_NE(scene.find("inner_bat"), nullptr);
    EXPECT_NE(scene.find("inner_lamp"), nullptr);
    EXPECT_NE(scene.find("inner_wire_0"), nullptr);
}

/// Verify that even if root blueprint has shadow nodes with a specific layout_group,
/// inline_def still renders independently when used directly.
TEST(EmbeddedSubwindowScene, InlineDefIndependentOfRootShadows) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    // Build inline definition
    bp2::Blueprint inline_bp;
    auto in1 = make_node(interner, "nested_bat", "Battery", "");
    auto in2 = make_node(interner, "nested_lamp", "Lamp", "");
    auto in_wire = make_wire(interner, arena, "nested_wire",
                             "nested_bat", "v_out", "nested_lamp", "v_in");
    inline_bp = inline_bp.with_node(std::move(in1));
    inline_bp = inline_bp.with_node(std::move(in2));
    inline_bp = inline_bp.with_wire(std::move(in_wire));

    // Create root blueprint with DIFFERENT shadow copies (same IDs, but different layout_group)
    // This simulates the old addBlueprint behavior
    bp2::Blueprint root_bp;
    auto shadow_n1 = make_node(interner, "nested_bat", "Battery", "composite_1");
    auto shadow_n2 = make_node(interner, "nested_lamp", "Lamp", "composite_1");
    auto shadow_wire = make_wire(interner, arena, "nested_wire",
                                 "nested_bat", "v_out", "nested_lamp", "v_in");
    root_bp = root_bp.with_node(std::move(shadow_n1));
    root_bp = root_bp.with_node(std::move(shadow_n2));
    root_bp = root_bp.with_wire(std::move(shadow_wire));

    // Rebuild scene from inline_bp (no group filtering, used for subwindow)
    visual::Scene inline_scene;
    visual::mutations::rebuild(inline_scene, inline_bp, interner, arena, "");
    EXPECT_EQ(inline_scene.roots().size(), 3u);

    // Rebuild scene from root_bp with layout_group filter (old approach, for comparison)
    visual::Scene root_scene_filtered;
    visual::mutations::rebuild(root_scene_filtered, root_bp, interner, arena, "composite_1");
    EXPECT_EQ(root_scene_filtered.roots().size(), 3u);

    // Both should have the same rendered content (nodes + wires)
    EXPECT_NE(inline_scene.find("nested_bat"), nullptr);
    EXPECT_NE(inline_scene.find("nested_lamp"), nullptr);
    EXPECT_NE(inline_scene.find("nested_wire"), nullptr);

    EXPECT_NE(root_scene_filtered.find("nested_bat"), nullptr);
    EXPECT_NE(root_scene_filtered.find("nested_lamp"), nullptr);
    EXPECT_NE(root_scene_filtered.find("nested_wire"), nullptr);
}

/// Verify that root-level nodes with empty layout_group are still rendered
/// when rebuilding root window.
TEST(EmbeddedSubwindowScene, RootWindowStillShowsRootNodes) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    // Build a blueprint with both root and grouped nodes
    bp2::Blueprint bp;
    auto root_node = make_node(interner, "root_bat", "Battery", "");
    auto group_node = make_node(interner, "composite_bat", "Battery", "composite_1");
    auto wire = make_wire(interner, arena, "wire_root",
                          "root_bat", "v_out", "composite_bat", "v_in");

    bp = bp.with_node(std::move(root_node));
    bp = bp.with_node(std::move(group_node));
    bp = bp.with_wire(std::move(wire));

    // Rebuild root window (empty layout_group)
    visual::Scene root_scene;
    visual::mutations::rebuild(root_scene, bp, interner, arena, "");

    // Should only show root-level node
    EXPECT_EQ(root_scene.roots().size(), 1u);
    EXPECT_NE(root_scene.find("root_bat"), nullptr);
    EXPECT_EQ(root_scene.find("composite_bat"), nullptr);

    // Rebuild subwindow (layout_group = "composite_1")
    visual::Scene sub_scene;
    visual::mutations::rebuild(sub_scene, bp, interner, arena, "composite_1");

    // Should only show grouped node
    EXPECT_EQ(sub_scene.roots().size(), 1u);
    EXPECT_EQ(sub_scene.find("root_bat"), nullptr);
    EXPECT_NE(sub_scene.find("composite_bat"), nullptr);
}

TEST(EmbeddedSubwindowScene, CompositeHostPortsUseNestedAuthorityNotCollapsedCache) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint inline_bp;
    inline_bp = inline_bp.with_interface(bp2::Interface({
        make_port(interner, "inner_only", Domain::Electrical, bp2::Direction::Input, PortType::V),
    }));

    bp2::Blueprint::Node collapsed;
    collapsed.semantic.id = interner.intern("composite_1");
    collapsed.semantic.type = interner.intern("CompositeType");
    collapsed.view.name = "composite_1";
    collapsed.view.expandable = true;
    collapsed.layout.layout_group = "";
    set_iface(collapsed, {
        make_port(interner, "stale_only", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("composite_1"), interner.intern("CompositeType"),
        std::make_unique<bp2::Blueprint>(inline_bp));

    bp2::Blueprint root;
    root = root.with_node(std::move(collapsed));
    root = root.with_nested(std::move(nested));

    visual::Scene root_scene;
    visual::mutations::rebuild(root_scene, root, interner, arena, "");

    auto* composite_widget = root_scene.find("composite_1");
    ASSERT_NE(composite_widget, nullptr);
    EXPECT_NE(composite_widget->portByName("inner_only"), nullptr);
    EXPECT_EQ(composite_widget->portByName("stale_only"), nullptr);

    auto* collapsed_node = root.find_node(interner.intern("composite_1"));
    ASSERT_NE(collapsed_node, nullptr);
    EXPECT_TRUE(collapsed_node->semantic.iface.has(interner.intern("stale_only")));
    EXPECT_FALSE(collapsed_node->semantic.iface.has(interner.intern("inner_only")));
}
