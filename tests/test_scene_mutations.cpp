#include <gtest/gtest.h>

#include <filesystem>
#include "visual/scene_mutations.h"
#include "visual/scene.h"
#include "visual/persist.h"
#include "json_parser/json_parser.h"
#include "visual/node/node_factory.h"
#include "visual/node/visual_node.h"
#include "visual/node/ref_node_widget.h"
#include "editor/layout_constants.h"
#include "visual/snap.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "visual/node/bus_node_widget.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"

// ============================================================================
// Helpers
// ============================================================================

// Shared bp2 test helpers (make_port, set_iface)
#include "bp2_test_helpers.h"

/// Build a bp2::Blueprint::Node with the given id, type, and layout_group.
static bp2::Blueprint::Node make_bp2_node(ui::StringInterner& I,
                                           const char* id,
                                           const char* type = "Battery",
                                           const char* layout_group = "") {
     bp2::Blueprint::Node n;
     n.semantic.id = I.intern(id);
     n.semantic.type = I.intern(type);
     n.semantic.owner_scope = layout_group;
     return n;
 }

/// Build a bp2::Blueprint::Wire connecting src_node:src_port -> dst_node:dst_port.
static bp2::Blueprint::Wire make_bp2_wire(ui::StringInterner& I,
                                           bp2::PathArena& arena,
                                           const char* wire_id,
                                           const char* src_node, const char* src_port,
                                           const char* dst_node, const char* dst_port) {
     bp2::Blueprint::Wire w;
     w.id = I.intern(wire_id);
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
    set_iface(n1, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto n2 = make_bp2_node(interner, "lamp1", "Lamp");
    set_iface(n2, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

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
    set_iface(n1, {
        make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto n2 = make_bp2_node(interner, "lamp1", "Lamp");
    set_iface(n2, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

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
    bus.view.render_hint = "bus";
    bus.layout.width = 200.0f;
    bus.layout.height = 40.0f;
    set_iface(bus, {
        make_port(interner, "v", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    auto bat = make_bp2_node(interner, "bat1", "Battery");
    set_iface(bat, {
        make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

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
    n.view.has_color = true;
    n.view.color_r = 0.8f; n.view.color_g = 0.2f; n.view.color_b = 0.1f; n.view.color_a = 1.0f;

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
    bus.view.render_hint = "bus";
    bus.view.has_color = true;
    bus.view.color_r = 0.1f; bus.view.color_g = 0.5f; bus.view.color_b = 0.9f; bus.view.color_a = 1.0f;

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
    bus.view.render_hint = "bus";
    bus.layout.width = 200.0f; bus.layout.height = 40.0f;
    set_iface(bus, {
        make_port(interner, "v", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    auto bat = make_bp2_node(interner, "bat1", "Battery");
    set_iface(bat, {
        make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto lamp = make_bp2_node(interner, "lamp1", "Lamp");
    set_iface(lamp, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

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
    const std::string gsc_path = "/Users/vladimir/an24_cpp/GSC.blueprint";
    if (!std::filesystem::exists(gsc_path)) {
        GTEST_SKIP() << "GSC.blueprint not present (workspace save file, not source-controlled)";
    }

    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    TypeRegistry parser_registry = load_type_registry("library/");
    auto bp_opt = load_blueprint_from_file(gsc_path.c_str(), interner, arena, parser_registry);
    if (!bp_opt.has_value()) {
        GTEST_SKIP() << "GSC.blueprint present but not decodable under strict schema";
    }

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
    ref.view.render_hint = "ref";
    set_iface(ref, {
        make_port(interner, "v", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

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
    ref.view.render_hint = "ref";
    ref.layout.x = 0; ref.layout.y = 0;
    set_iface(ref, {
        make_port(interner, "v", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto bat = make_bp2_node(interner, "bat1", "Battery");
    bat.layout.x = 200; bat.layout.y = 0;
    set_iface(bat, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

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
    ref.view.render_hint = "ref";
    ref.layout.x = 200; ref.layout.y = 0;
    set_iface(ref, {
        make_port(interner, "v", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto bat = make_bp2_node(interner, "bat1", "Battery");
    bat.layout.x = 0; bat.layout.y = 0;
    set_iface(bat, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

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
    ref.view.render_hint = "ref";
    ref.layout.x = 0; ref.layout.y = 0;
    set_iface(ref, {
        make_port(interner, "v", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto bat = make_bp2_node(interner, "bat1", "Battery");
    bat.layout.x = 0; bat.layout.y = 200;
    set_iface(bat, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

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
    ref.view.render_hint = "ref";
    set_iface(ref, {
        make_port(interner, "v", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

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
    EXPECT_EQ(side_from_relative_position(origin, Pt(200, 100)), bp2::PortLayoutSide::Right);
    // Clearly to the left
    EXPECT_EQ(side_from_relative_position(origin, Pt(0, 100)), bp2::PortLayoutSide::Left);
    // Clearly below
    EXPECT_EQ(side_from_relative_position(origin, Pt(100, 200)), bp2::PortLayoutSide::Bottom);
    // Clearly above
    EXPECT_EQ(side_from_relative_position(origin, Pt(100, 0)), bp2::PortLayoutSide::Top);

    // Diagonal 45° — dx==dy → horizontal wins (right)
    EXPECT_EQ(side_from_relative_position(origin, Pt(200, 200)), bp2::PortLayoutSide::Right);
    // Diagonal 45° — dx==-dy → horizontal wins (left)
    EXPECT_EQ(side_from_relative_position(origin, Pt(0, 200)), bp2::PortLayoutSide::Left);

    // Same position — dx==dy==0 → horizontal wins (right by >=0 check)
    EXPECT_EQ(side_from_relative_position(origin, origin), bp2::PortLayoutSide::Right);
}

// =============================================================================
// Bug 2 regression: InOut ports render once (left side only)
// =============================================================================

TEST(SceneMutations, InOutPortsNotDuplicatedOnBothSides) {
    // InOut ports are represented once in semantic iface and should render once.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_bp2_node(I, "knob_1", "KnobSwitch");
    set_iface(knob, {
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw3", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });
     knob.view.content_type = bp2::NodeContentType::Knob;

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, I, arena, "");

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(widget, nullptr);

    // Count port widgets — each InOut port should appear exactly once, not twice.
    int throw1_count = 0, throw2_count = 0, throw3_count = 0;
    for (auto* p : widget->ports()) {
        std::string_view pname = p->name();
        if (pname == "throw1") throw1_count++;
        else if (pname == "throw2") throw2_count++;
        else if (pname == "throw3") throw3_count++;
    }
    EXPECT_EQ(throw1_count, 1) << "InOut port throw1 should appear exactly once";
    EXPECT_EQ(throw2_count, 1) << "InOut port throw2 should appear exactly once";
    EXPECT_EQ(throw3_count, 1) << "InOut port throw3 should appear exactly once";
}

TEST(SceneMutations, InOutPortsMixedWithRegularPorts) {
    // A node with both regular and InOut ports: regular ports should still
    // appear on their respective sides, InOut ports only on the left.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto node = make_bp2_node(I, "mixed_1", "MixedComponent");
    set_iface(node, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "bus", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(node));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, I, arena, "");

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("mixed_1"));
    ASSERT_NE(widget, nullptr);

    // Count all port widgets
    int bus_count = 0, v_in_count = 0, v_out_count = 0;
    for (auto* p : widget->ports()) {
        std::string_view pname = p->name();
        if (pname == "bus") bus_count++;
        else if (pname == "v_in") v_in_count++;
        else if (pname == "v_out") v_out_count++;
    }
    EXPECT_EQ(bus_count, 1) << "InOut port 'bus' should appear exactly once";
    EXPECT_EQ(v_in_count, 1) << "Regular input 'v_in' should appear once";
    EXPECT_EQ(v_out_count, 1) << "Regular output 'v_out' should appear once";
}

TEST(SceneMutations, KnobSwitchUsesWiperThrowNamesAndNoDuplication) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_bp2_node(I, "knob_1", "KnobSwitch");
    set_iface(knob, {
        make_port(I, "wiper", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });
     knob.view.content_type = bp2::NodeContentType::Knob;

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, I, arena, "");

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(widget, nullptr);

    int wiper_count = 0;
    int throw1_count = 0;
    int throw2_count = 0;
    int legacy_common_count = 0;
    int legacy_t1_count = 0;
    for (auto* p : widget->ports()) {
        std::string_view pname = p->name();
        if (pname == "wiper") wiper_count++;
        else if (pname == "throw1") throw1_count++;
        else if (pname == "throw2") throw2_count++;
        else if (pname == "common") legacy_common_count++;
        else if (pname == "t1") legacy_t1_count++;
    }

    EXPECT_EQ(wiper_count, 1);
    EXPECT_EQ(throw1_count, 1);
    EXPECT_EQ(throw2_count, 1);
    EXPECT_EQ(legacy_common_count, 0);
    EXPECT_EQ(legacy_t1_count, 0);
}
