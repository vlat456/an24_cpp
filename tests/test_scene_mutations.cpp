#include <gtest/gtest.h>

#include <filesystem>
#include "visual/scene_mutations.h"
#include "visual/scene.h"
#include "visual/persist.h"
#include "visual/node/node_factory.h"
#include "visual/node/visual_node.h"
#include "visual/node/ref_node_widget.h"
#include "editor/layout_constants.h"
#include "visual/snap.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "visual/node/bus_node_widget.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "editor/data/port.h"
#include "ui/core/interned_id.h"

// ============================================================================
// Helpers
// ============================================================================

/// Build a bp2::Blueprint::Node with the given id, type, and group_id.
static bp2::Blueprint::Node make_bp2_node(ui::StringInterner& I,
                                           const char* id,
                                           const char* type = "Battery",
                                           const char* group_id = "") {
    bp2::Blueprint::Node n;
    n.id       = I.intern(id);
    n.type     = I.intern(type);
    n.group_id = group_id;
    return n;
}

/// Build a bp2::Blueprint::Wire connecting src_node:src_port -> dst_node:dst_port.
static bp2::Blueprint::Wire make_bp2_wire(ui::StringInterner& I,
                                           bp2::PathArena& arena,
                                           const char* wire_id,
                                           const char* src_node, const char* src_port,
                                           const char* dst_node, const char* dst_port) {
    bp2::Blueprint::Wire w;
    w.id     = I.intern(wire_id);
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern(src_node)),
                               I.intern(src_port));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern(dst_node)),
                               I.intern(dst_port));
    return w;
}

// ============================================================================
// rebuild
// ============================================================================

TEST(SceneMutations, RebuildCreatesNodeWidgets) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n1 = make_bp2_node(interner, "bat1", "Battery");
    n1.inputs.push_back(EditorPort(interner.intern("v_in"), PortSide::Input, PortType::V));
    n1.outputs.push_back(EditorPort(interner.intern("v_out"), PortSide::Output, PortType::V));

    auto n2 = make_bp2_node(interner, "lamp1", "Lamp");
    n2.inputs.push_back(EditorPort(interner.intern("v_in"), PortSide::Input, PortType::V));

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n1));
    bp = bp.with_node(std::move(n2));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    EXPECT_EQ(scene.roots().size(), 2u);
    EXPECT_NE(scene.find("bat1"), nullptr);
    EXPECT_NE(scene.find("lamp1"), nullptr);
}

TEST(SceneMutations, RebuildFiltersGroupId) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n1 = make_bp2_node(interner, "bat1", "Battery", "");
    auto n2 = make_bp2_node(interner, "inner1", "Lamp", "group_A");

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n1));
    bp = bp.with_node(std::move(n2));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    // Only the root-level node should appear
    EXPECT_EQ(scene.roots().size(), 1u);
    EXPECT_NE(scene.find("bat1"), nullptr);
    EXPECT_EQ(scene.find("inner1"), nullptr);
}

TEST(SceneMutations, RebuildCreatesWireWidgets) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n1 = make_bp2_node(interner, "bat1", "Battery");
    n1.outputs.push_back(EditorPort(interner.intern("v_out"), PortSide::Output, PortType::V));

    auto n2 = make_bp2_node(interner, "lamp1", "Lamp");
    n2.inputs.push_back(EditorPort(interner.intern("v_in"), PortSide::Input, PortType::V));

    auto wire = make_bp2_wire(interner, arena, "wire_0",
                               "bat1", "v_out", "lamp1", "v_in");

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n1));
    bp = bp.with_node(std::move(n2));
    bp = bp.with_wire(std::move(wire));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    // 2 nodes + 1 wire = 3 roots
    EXPECT_EQ(scene.roots().size(), 3u);
    EXPECT_NE(scene.find("wire_0"), nullptr);
}

TEST(SceneMutations, RebuildClearsExistingScene) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n1 = make_bp2_node(interner, "bat1", "Battery");

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n1));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");
    EXPECT_EQ(scene.roots().size(), 1u);

    // Rebuild again — should clear first
    visual::mutations::rebuild(scene, bp, interner, arena, "");
    EXPECT_EQ(scene.roots().size(), 1u);
}

// ============================================================================
// Bus node wire operations
// ============================================================================

TEST(SceneMutations, RebuildWithBusNodeCreatesAliasPortWires) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    // Bus node with render_hint="bus"
    auto bus = make_bp2_node(interner, "bus1", "Bus");
    bus.render_hint = "bus";
    bus.width  = 200.0f;
    bus.height = 40.0f;
    bus.inputs.push_back(EditorPort(interner.intern("v"), PortSide::Input, PortType::V));

    auto bat = make_bp2_node(interner, "bat1", "Battery");
    bat.outputs.push_back(EditorPort(interner.intern("v_out"), PortSide::Output, PortType::V));

    auto wire = make_bp2_wire(interner, arena, "wire_0",
                               "bat1", "v_out", "bus1", "v");

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bus));
    bp = bp.with_node(std::move(bat));
    bp = bp.with_wire(std::move(wire));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    // 2 nodes + 1 wire
    EXPECT_EQ(scene.roots().size(), 3u);
    EXPECT_NE(scene.find("bus1"), nullptr);
    EXPECT_NE(scene.find("wire_0"), nullptr);
}

// ============================================================================
// REGRESSION: Scene rebuild preserves custom colors from data layer
// ============================================================================

TEST(SceneMutations, RebuildPreservesNodeColor) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n = make_bp2_node(interner, "bat1", "Battery");
    n.has_color = true;
    n.color_r = 0.8f; n.color_g = 0.2f; n.color_b = 0.1f; n.color_a = 1.0f;

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    auto* w = scene.find("bat1");
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->customColor().has_value());
}

TEST(SceneMutations, RebuildPreservesBusNodeColor) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto bus = make_bp2_node(interner, "bus1", "Bus");
    bus.render_hint = "bus";
    bus.has_color = true;
    bus.color_r = 0.1f; bus.color_g = 0.5f; bus.color_b = 0.9f; bus.color_a = 1.0f;

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bus));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    auto* w = scene.find("bus1");
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->customColor().has_value());
}

TEST(SceneMutations, RebuildNoColorWhenNodeHasNoColor) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n = make_bp2_node(interner, "bat1", "Battery");
    // has_color defaults to false

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    auto* w = scene.find("bat1");
    ASSERT_NE(w, nullptr);
    EXPECT_FALSE(w->customColor().has_value());
}

// ============================================================================
// Multiple wires on bus rebuild correctly
// ============================================================================

TEST(SceneMutations, RebuildMultipleBusWires) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto bus = make_bp2_node(interner, "bus1", "Bus");
    bus.render_hint = "bus";
    bus.width = 200.0f; bus.height = 40.0f;
    bus.inputs.push_back(EditorPort(interner.intern("v"), PortSide::Input, PortType::V));

    auto bat = make_bp2_node(interner, "bat1", "Battery");
    bat.outputs.push_back(EditorPort(interner.intern("v_out"), PortSide::Output, PortType::V));

    auto lamp = make_bp2_node(interner, "lamp1", "Lamp");
    lamp.inputs.push_back(EditorPort(interner.intern("v_in"), PortSide::Input, PortType::V));

    auto w0 = make_bp2_wire(interner, arena, "w0", "bat1",  "v_out", "bus1",  "v");
    auto w1 = make_bp2_wire(interner, arena, "w1", "bus1",  "v",     "lamp1", "v_in");

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bus));
    bp = bp.with_node(std::move(bat));
    bp = bp.with_node(std::move(lamp));
    bp = bp.with_wire(std::move(w0));
    bp = bp.with_wire(std::move(w1));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    // 3 nodes + 2 wires = 5
    EXPECT_EQ(scene.roots().size(), 5u);
    EXPECT_NE(scene.find("w0"), nullptr);
    EXPECT_NE(scene.find("w1"), nullptr);

    // Bus should have alias ports for both wires
    auto* bus_widget = dynamic_cast<visual::BusNodeWidget*>(scene.find("bus1"));
    ASSERT_NE(bus_widget, nullptr);
    EXPECT_NE(bus_widget->port("w0"), nullptr);
    EXPECT_NE(bus_widget->port("w1"), nullptr);
}

TEST(SceneMutations, Regression_GSCLoadHasPortsAndWiresVisible) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto bp_opt = load_blueprint_from_file("/Users/vladimir/an24_cpp/GSC.blueprint", interner, arena);
    ASSERT_TRUE(bp_opt.has_value());

    visual::Scene scene;
    visual::mutations::rebuild(scene, *bp_opt, interner, arena, "");

    auto* pi_widget_base = scene.find("pi_1");
    ASSERT_NE(pi_widget_base, nullptr);
    auto* pi_widget = dynamic_cast<visual::NodeWidget*>(pi_widget_base);
    ASSERT_NE(pi_widget, nullptr);

    // A known top-level wire in GSC.blueprint should be rendered as a widget.
    // Note: wire_200 is the actual ID in the blueprint (wire_20 was stale).
    EXPECT_NE(scene.find("wire_200"), nullptr);
}

TEST(SceneMutations, RefNodePortCenteredOnNodeWidth) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto ref = make_bp2_node(interner, "ref1", "RefNode");
    ref.render_hint = "ref";
    ref.outputs.push_back(EditorPort(interner.intern("v"), PortSide::Output, PortType::V));

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(ref));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(scene.find("ref1"));
    ASSERT_NE(ref_widget, nullptr);

    auto* p = ref_widget->port("v");
    ASSERT_NE(p, nullptr);

    // Default port_layout_side_ is Top: port center should be at half the node width.
    const float center_x = p->localPos().x + visual::PortConstants::RADIUS;
    const float expected_center_x = ref_widget->size().x * 0.5f;
    EXPECT_NEAR(center_x, expected_center_x, 1e-4f);
}

// ============================================================================
// Ref node auto-orientation
// ============================================================================

TEST(SceneMutations, RefNodeOrientsFacingConnectedNode_Right) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    // ref at (0,0), battery at (200,0) → ref should face right
    auto ref = make_bp2_node(interner, "ref1", "RefNode");
    ref.render_hint = "ref";
    ref.x = 0; ref.y = 0;
    ref.outputs.push_back(EditorPort(interner.intern("v"), PortSide::Output, PortType::V));

    auto bat = make_bp2_node(interner, "bat1", "Battery");
    bat.x = 200; bat.y = 0;
    bat.inputs.push_back(EditorPort(interner.intern("v_in"), PortSide::Input, PortType::V));

    auto wire = make_bp2_wire(interner, arena, "w1", "ref1", "v", "bat1", "v_in");

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(ref));
    bp = bp.with_node(std::move(bat));
    bp = bp.with_wire(std::move(wire));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(scene.find("ref1"));
    ASSERT_NE(ref_widget, nullptr);

    auto* p = ref_widget->port("v");
    ASSERT_NE(p, nullptr);
    // Port should be on Right edge → port center x near node width
    float port_center_x = p->localPos().x + visual::PortConstants::RADIUS;
    EXPECT_NEAR(port_center_x, ref_widget->size().x, 1e-4f);
}

TEST(SceneMutations, RefNodeOrientsFacingConnectedNode_Left) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    // ref at (200,0), battery at (0,0) → ref should face left
    auto ref = make_bp2_node(interner, "ref1", "RefNode");
    ref.render_hint = "ref";
    ref.x = 200; ref.y = 0;
    ref.outputs.push_back(EditorPort(interner.intern("v"), PortSide::Output, PortType::V));

    auto bat = make_bp2_node(interner, "bat1", "Battery");
    bat.x = 0; bat.y = 0;
    bat.inputs.push_back(EditorPort(interner.intern("v_in"), PortSide::Input, PortType::V));

    auto wire = make_bp2_wire(interner, arena, "w1", "ref1", "v", "bat1", "v_in");

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(ref));
    bp = bp.with_node(std::move(bat));
    bp = bp.with_wire(std::move(wire));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(scene.find("ref1"));
    ASSERT_NE(ref_widget, nullptr);

    auto* p = ref_widget->port("v");
    ASSERT_NE(p, nullptr);
    // Port should be on Left edge → port center x near 0
    float port_center_x = p->localPos().x + visual::PortConstants::RADIUS;
    EXPECT_NEAR(port_center_x, 0.0f, 1e-4f);
}

TEST(SceneMutations, RefNodeOrientsFacingConnectedNode_Bottom) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    // ref at (0,0), battery at (0,200) → ref should face bottom
    auto ref = make_bp2_node(interner, "ref1", "RefNode");
    ref.render_hint = "ref";
    ref.x = 0; ref.y = 0;
    ref.outputs.push_back(EditorPort(interner.intern("v"), PortSide::Output, PortType::V));

    auto bat = make_bp2_node(interner, "bat1", "Battery");
    bat.x = 0; bat.y = 200;
    bat.inputs.push_back(EditorPort(interner.intern("v_in"), PortSide::Input, PortType::V));

    auto wire = make_bp2_wire(interner, arena, "w1", "ref1", "v", "bat1", "v_in");

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(ref));
    bp = bp.with_node(std::move(bat));
    bp = bp.with_wire(std::move(wire));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(scene.find("ref1"));
    ASSERT_NE(ref_widget, nullptr);

    auto* p = ref_widget->port("v");
    ASSERT_NE(p, nullptr);
    // Port should be on Bottom edge → port center y near node height
    float port_center_y = p->localPos().y + visual::PortConstants::RADIUS;
    EXPECT_NEAR(port_center_y, ref_widget->size().y, 1e-4f);
}

TEST(SceneMutations, RefNodeWithoutWireKeepsDefaultTopOrientation) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto ref = make_bp2_node(interner, "ref1", "RefNode");
    ref.render_hint = "ref";
    ref.outputs.push_back(EditorPort(interner.intern("v"), PortSide::Output, PortType::V));

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(ref));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(scene.find("ref1"));
    ASSERT_NE(ref_widget, nullptr);

    auto* p = ref_widget->port("v");
    ASSERT_NE(p, nullptr);
    // No wire → stays Top (default). Port center y should be at -RADIUS (top edge).
    float port_center_y = p->localPos().y + visual::PortConstants::RADIUS;
    EXPECT_NEAR(port_center_y, 0.0f, 1e-4f);
}

// ============================================================================
// snap_to_half_grid
// ============================================================================

TEST(SnapMath, SnapToHalfGrid) {
    float grid = 16.0f;
    // Exact half-grid points remain unchanged
    auto r1 = editor_math::snap_to_half_grid(ui::Pt(8.0f, 8.0f), grid);
    EXPECT_NEAR(r1.x, 8.0f, 1e-4f);
    EXPECT_NEAR(r1.y, 8.0f, 1e-4f);

    // Snaps to nearest half-grid (0, 8, 16, 24, ...)
    auto r2 = editor_math::snap_to_half_grid(ui::Pt(3.0f, 11.0f), grid);
    EXPECT_NEAR(r2.x, 0.0f, 1e-4f);   // 3 rounds to 0
    EXPECT_NEAR(r2.y, 8.0f, 1e-4f);   // 11 rounds to 8

    auto r3 = editor_math::snap_to_half_grid(ui::Pt(5.0f, 13.0f), grid);
    EXPECT_NEAR(r3.x, 8.0f, 1e-4f);   // 5 rounds to 8
    EXPECT_NEAR(r3.y, 16.0f, 1e-4f);  // 13 rounds to 16

    // Full grid points are also half-grid points
    auto r4 = editor_math::snap_to_half_grid(ui::Pt(16.0f, 32.0f), grid);
    EXPECT_NEAR(r4.x, 16.0f, 1e-4f);
    EXPECT_NEAR(r4.y, 32.0f, 1e-4f);

    // Guard: zero grid step returns input unchanged
    auto r5 = editor_math::snap_to_half_grid(ui::Pt(7.3f, 2.1f), 0.0f);
    EXPECT_NEAR(r5.x, 7.3f, 1e-4f);
    EXPECT_NEAR(r5.y, 2.1f, 1e-4f);
}

// ============================================================================
// side_from_relative_position
// ============================================================================

TEST(SnapMath, SideFromRelativePosition) {
    using editor_math::side_from_relative_position;
    using ui::Pt;

    Pt origin(100, 100);

    // Clearly to the right
    EXPECT_EQ(side_from_relative_position(origin, Pt(200, 100)), PortLayoutSide::Right);
    // Clearly to the left
    EXPECT_EQ(side_from_relative_position(origin, Pt(0, 100)), PortLayoutSide::Left);
    // Clearly below
    EXPECT_EQ(side_from_relative_position(origin, Pt(100, 200)), PortLayoutSide::Bottom);
    // Clearly above
    EXPECT_EQ(side_from_relative_position(origin, Pt(100, 0)), PortLayoutSide::Top);

    // Diagonal 45° — dx==dy → horizontal wins (right)
    EXPECT_EQ(side_from_relative_position(origin, Pt(200, 200)), PortLayoutSide::Right);
    // Diagonal 45° — dx==-dy → horizontal wins (left)
    EXPECT_EQ(side_from_relative_position(origin, Pt(0, 200)), PortLayoutSide::Left);

    // Same position — dx==dy==0 → horizontal wins (right by >=0 check)
    EXPECT_EQ(side_from_relative_position(origin, origin), PortLayoutSide::Right);
}
