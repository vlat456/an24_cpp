#include <gtest/gtest.h>

#include "editor/visual/presentation/canvas_scene_snapshot.h"
#include "editor/layout_constants.h"
#include "visual/scene.h"
#include "visual/scene_mutations.h"
#include "visual/node/visual_node.h"
#include "visual/node/group_node_widget.h"
#include "visual/port/visual_port.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "bp2_test_helpers.h"

static bp2::Blueprint::Node make_canvas_snapshot_node(ui::StringInterner& I,
                                                      const char* id,
                                                      const char* type = "Battery") {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern(type);
    n.view.name = id;
    return n;
}

static bp2::Blueprint::Wire make_canvas_snapshot_wire(ui::StringInterner& I,
                                                      const char* wire_id,
                                                      const char* src_node,
                                                      const char* src_port,
                                                      const char* dst_node,
                                                      const char* dst_port) {
    bp2::Blueprint::Wire w;
    w.id = I.intern(wire_id);
    w.source = bp2::WireEndpoint{I.intern(src_node), I.intern(src_port)};
    w.target = bp2::WireEndpoint{I.intern(dst_node), I.intern(dst_port)};
    return w;
}

TEST(CanvasSceneSnapshot, RecursivelyProjectsPortsAndRoutingPoints) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n1 = make_canvas_snapshot_node(interner, "bat1", "Battery");
    n1.layout.x = 0.0f;
    n1.layout.y = 0.0f;
    set_iface(n1, {
        make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto n2 = make_canvas_snapshot_node(interner, "lamp1", "Lamp");
    n2.layout.x = 120.0f;
    n2.layout.y = 0.0f;
    set_iface(n2, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    auto w = make_canvas_snapshot_wire(interner, "wire1", "bat1", "v_out", "lamp1", "v_in");
    w.routing_points.push_back({60.0f, 24.0f});

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n1));
    bp = bp.with_node(std::move(n2));
    bp = bp.with_wire(std::move(w));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    const auto snapshot = editor::presentation::build_canvas_scene_snapshot(scene, interner);

    size_t port_hit_count = 0;
    size_t routing_hit_count = 0;
    size_t wire_hit_count = 0;
    for (const auto& hit : snapshot.hit_objects) {
        if (hit.kind == editor::presentation::CanvasHitObjectKind::Port) {
            ++port_hit_count;
        }
        if (hit.kind == editor::presentation::CanvasHitObjectKind::RoutingPoint) {
            ++routing_hit_count;
        }
        if (hit.kind == editor::presentation::CanvasHitObjectKind::WireSegment) {
            ++wire_hit_count;
        }
    }

    EXPECT_EQ(port_hit_count, 2u);
    EXPECT_EQ(routing_hit_count, 1u);
    EXPECT_GE(wire_hit_count, 1u);
}

TEST(CanvasSceneSnapshot, ContentObjectsAreProjectedToAbsoluteCanvasCoordinates) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto knob = make_canvas_snapshot_node(interner, "knob1", "KnobSwitch");
    knob.layout.x = 100.0f;
    knob.layout.y = 50.0f;
    knob.view.content_type = bp2::NodeContentType::Knob;
    knob.view.content_value = 1.0f;
    knob.view.content_max = 4.0f;
    set_iface(knob, {
        make_port(interner, "common", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "throw_1", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob1"));
    ASSERT_NE(widget, nullptr);

    const auto snapshot = editor::presentation::build_canvas_scene_snapshot(scene, interner);
    bool found_absolute_content = false;
    for (const auto& obj : snapshot.render_objects) {
        if (obj.kind != editor::presentation::CanvasRenderObjectKind::ContentPaint) {
            continue;
        }
        if (obj.node_id != interner.lookup("knob1")) {
            continue;
        }
        if (obj.bounds.x >= widget->worldPos().x && obj.bounds.y >= widget->worldPos().y) {
            found_absolute_content = true;
            break;
        }
    }

    EXPECT_TRUE(found_absolute_content);
}

// ============================================================================
// Regression: Group node border-only hit semantics
// ============================================================================

TEST(CanvasSceneSnapshot, GroupNodeBorderOnlyHitSemantics) {
    // A group node should only be hit on its title bar and border margins.
    // Clicking the interior should return HitEmpty (pass-through to nodes behind).
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint::Node group;
    group.semantic.id = interner.intern("grp1");
    group.semantic.type = interner.intern("Group");
    group.view.name = "grp1";
    group.view.render_hint = "group";
    group.layout.x = 100.0f;
    group.layout.y = 100.0f;
    group.layout.width = 200.0f;
    group.layout.height = 200.0f;

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(group));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    auto* grp_widget = dynamic_cast<visual::GroupNodeWidget*>(scene.find("grp1"));
    ASSERT_NE(grp_widget, nullptr) << "Group widget must exist in scene";

    const auto snapshot = editor::presentation::build_canvas_scene_snapshot(scene, interner);

    // Verify group hit object exists and has is_group flag
    const editor::presentation::CanvasHitObject* group_hit = nullptr;
    for (const auto& h : snapshot.hit_objects) {
        if (h.kind == editor::presentation::CanvasHitObjectKind::NodeBody &&
            h.node_id == interner.lookup("grp1")) {
            group_hit = &h;
            break;
        }
    }
    ASSERT_NE(group_hit, nullptr) << "Snapshot must contain group node hit object";
    EXPECT_TRUE(group_hit->is_group);

    // Title bar click (y near top) → should hit the group
    const float title_h = editor_constants::GROUP_TITLE_PADDING * 2
                        + editor_constants::Font::Medium;
    ui::Pt title_click(200.0f, 100.0f + title_h * 0.5f);  // middle of title
    auto title_result = editor::presentation::hit_test_canvas_scene(snapshot, title_click, interner);
    ASSERT_TRUE(std::holds_alternative<visual::HitNode>(title_result))
        << "Clicking title bar of group must return HitNode";
    EXPECT_EQ(std::get<visual::HitNode>(title_result).node_id, "grp1");

    // Left border click → should hit the group
    const float m = editor_constants::GROUP_BORDER_HIT_MARGIN;
    ui::Pt left_border(100.0f + m * 0.5f, 200.0f);  // inside left margin
    auto left_result = editor::presentation::hit_test_canvas_scene(snapshot, left_border, interner);
    ASSERT_TRUE(std::holds_alternative<visual::HitNode>(left_result))
        << "Clicking left border of group must return HitNode";

    // Interior click (well inside all borders) → should NOT hit the group
    ui::Pt interior(200.0f, 250.0f);  // center of group, far from any border
    auto interior_result = editor::presentation::hit_test_canvas_scene(snapshot, interior, interner);
    EXPECT_TRUE(std::holds_alternative<visual::HitEmpty>(interior_result))
        << "Clicking interior of group must pass through (HitEmpty)";
}

// ============================================================================
// Regression: Wire segment distance-based hit testing
// ============================================================================

TEST(CanvasSceneSnapshot, WireSegmentHitTestUsesDistanceThreshold) {
    // A horizontal wire segment should be hittable within WIRE_TOLERANCE pixels,
    // but not outside that threshold.
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n1 = make_canvas_snapshot_node(interner, "a", "Battery");
    n1.layout.x = 0.0f;
    n1.layout.y = 0.0f;
    set_iface(n1, {
        make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto n2 = make_canvas_snapshot_node(interner, "b", "Lamp");
    n2.layout.x = 300.0f;
    n2.layout.y = 0.0f;
    set_iface(n2, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    auto w = make_canvas_snapshot_wire(interner, "w1", "a", "v_out", "b", "v_in");

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n1));
    bp = bp.with_node(std::move(n2));
    bp = bp.with_wire(std::move(w));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    // Find the wire to read its polyline
    auto* wire_widget = dynamic_cast<visual::Wire*>(scene.find(interner.resolve(interner.lookup("w1"))));
    ASSERT_NE(wire_widget, nullptr);
    const auto& polyline = wire_widget->polyline();
    ASSERT_GE(polyline.size(), 2u);

    const auto snapshot = editor::presentation::build_canvas_scene_snapshot(scene, interner);

    // Pick a point exactly on the first segment's midpoint
    ui::Pt seg_mid((polyline[0].x + polyline[1].x) * 0.5f,
                   (polyline[0].y + polyline[1].y) * 0.5f);

    // 1 pixel off the segment (within tolerance) → should hit wire
    ui::Pt near_wire(seg_mid.x, seg_mid.y + 2.0f);
    auto near_result = editor::presentation::hit_test_canvas_scene(snapshot, near_wire, interner);
    EXPECT_TRUE(std::holds_alternative<visual::HitWire>(near_result))
        << "Point within WIRE_TOLERANCE of segment must return HitWire";

    // 20 pixels off the segment (well outside tolerance) → should NOT hit wire
    ui::Pt far_from_wire(seg_mid.x, seg_mid.y + 20.0f);
    auto far_result = editor::presentation::hit_test_canvas_scene(snapshot, far_from_wire, interner);
    EXPECT_FALSE(std::holds_alternative<visual::HitWire>(far_result))
        << "Point far from segment must NOT return HitWire";
}

// ============================================================================
// Regression: Resize handles are projected for resizable (group) nodes
// ============================================================================

TEST(CanvasSceneSnapshot, ResizeHandlesProjectedForResizableGroupNode) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint::Node group;
    group.semantic.id = interner.intern("grp2");
    group.semantic.type = interner.intern("Group");
    group.view.name = "grp2";
    group.view.render_hint = "group";
    group.layout.x = 50.0f;
    group.layout.y = 50.0f;
    group.layout.width = 160.0f;
    group.layout.height = 120.0f;

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(group));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    const auto snapshot = editor::presentation::build_canvas_scene_snapshot(scene, interner);

    // Count resize handle hit objects for grp2
    size_t handle_count = 0;
    bool found_top_left = false;
    bool found_top_right = false;
    bool found_bottom_left = false;
    bool found_bottom_right = false;
    for (const auto& h : snapshot.hit_objects) {
        if (h.kind != editor::presentation::CanvasHitObjectKind::ResizeHandle) continue;
        if (h.node_id != interner.lookup("grp2")) continue;
        ++handle_count;
        switch (h.corner) {
            case ResizeCorner::TopLeft:     found_top_left = true; break;
            case ResizeCorner::TopRight:    found_top_right = true; break;
            case ResizeCorner::BottomLeft:  found_bottom_left = true; break;
            case ResizeCorner::BottomRight: found_bottom_right = true; break;
        }
    }
    EXPECT_EQ(handle_count, 4u) << "Resizable group must project exactly 4 resize handles";
    EXPECT_TRUE(found_top_left);
    EXPECT_TRUE(found_top_right);
    EXPECT_TRUE(found_bottom_left);
    EXPECT_TRUE(found_bottom_right);

    // Hit test on the bottom-right corner of the group widget
    auto* grp_widget = dynamic_cast<visual::GroupNodeWidget*>(scene.find("grp2"));
    ASSERT_NE(grp_widget, nullptr);
    ui::Pt br_corner = grp_widget->worldMax() - ui::Pt(2.0f, 2.0f);  // inside handle
    auto br_result = editor::presentation::hit_test_canvas_scene(snapshot, br_corner, interner);
    ASSERT_TRUE(std::holds_alternative<visual::HitResizeHandle>(br_result))
        << "Clicking bottom-right corner of resizable group must return HitResizeHandle";
    EXPECT_EQ(std::get<visual::HitResizeHandle>(br_result).corner, ResizeCorner::BottomRight);
}

// ============================================================================
// Regression: Hit test priority ordering (Port > RoutingPoint > ResizeHandle > Node > Wire)
// ============================================================================

TEST(CanvasSceneSnapshot, HitTestPriorityPortOverNode) {
    // When a port overlaps a node body, the port should win.
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n1 = make_canvas_snapshot_node(interner, "bat1", "Battery");
    n1.layout.x = 0.0f;
    n1.layout.y = 0.0f;
    set_iface(n1, {
        make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n1));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, "");

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("bat1"));
    ASSERT_NE(widget, nullptr);

    // Find the port widget and its center
    auto* port_widget = widget->portByName("v_out");
    ASSERT_NE(port_widget, nullptr);
    ui::Pt port_center = port_widget->worldPos()
                       + ui::Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);

    const auto snapshot = editor::presentation::build_canvas_scene_snapshot(scene, interner);

    // Click exactly on the port center — port overlaps node body
    auto result = editor::presentation::hit_test_canvas_scene(snapshot, port_center, interner);
    ASSERT_TRUE(std::holds_alternative<visual::HitPort>(result))
        << "Port must have higher priority than NodeBody in hit testing";
    EXPECT_EQ(std::get<visual::HitPort>(result).port_name, "v_out");
}
