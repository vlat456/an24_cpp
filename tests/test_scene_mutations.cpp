#include <gtest/gtest.h>

#include <filesystem>
#include "visual/scene_mutations.h"
#include "visual/scene.h"
#include "visual/persist.h"
#include "io/json/component_registry_json_loader.h"
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
#include "core/strings/interned_id.h"
#include "blueprint_v2/blueprint/node_content_type.h"

// ============================================================================
// Helpers
// ============================================================================

// Shared bp2 test helpers (make_port, set_iface)
#include "bp2_test_helpers.h"

/// Registry with all types used in scene mutation tests.
static ComponentRegistry make_scene_test_registry() {
    ComponentRegistry reg;
    auto add = [&](const char* name, const char* hint = "") {
        CompositeSpec def;
        def.classname = name;
        TypePresentation pres;
        if (hint && hint[0]) {
            pres.render_hint = hint;
        }
        reg.register_type(name, def, pres);
    };
    add("Battery");
    add("Lamp");
    add("Bus", "bus");
    add("RefNode", "ref");
    add("KnobSwitch");
    add("MixedComponent");
    add("VariableConductance");
    add("VCon");
    add("T");
    add("IndicatorLight");
    add("HostType");
    return reg;
}

static const ComponentRegistry& scene_reg() {
    static const ComponentRegistry r = make_scene_test_registry();
    return r;
}

/// Build a bp2::Blueprint::Node with the given id, type.
static bp2::Blueprint::Node make_bp2_node(core::StringInterner& I,
                                           const char* id,
                                           const char* type = "Battery") {
     bp2::Blueprint::Node n;
     n.semantic.id = I.intern(id);
     n.semantic.type = I.intern(type);
     return n;
 }

/// Build a bp2::Blueprint::Wire connecting src_node:src_port -> dst_node:dst_port.
static bp2::Blueprint::Wire make_bp2_wire(core::StringInterner& I,
                                           bp2::PathArena& /*arena*/,
                                           const char* wire_id,
                                           const char* src_node, const char* src_port,
                                           const char* dst_node, const char* dst_port) {
     bp2::Blueprint::Wire w;
     w.id = I.intern(wire_id);
    w.source = bp2::WireEndpoint{I.intern(src_node), I.intern(src_port)};
    w.target = bp2::WireEndpoint{I.intern(dst_node), I.intern(dst_port)};
    return w;
}

// ============================================================================
// rebuild
// ============================================================================

TEST(SceneMutations, RebuildCreatesNodeWidgets) {
    core::StringInterner interner;
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
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    EXPECT_EQ(scene.roots().size(), 2u);
    EXPECT_NE(scene.find("bat1"), nullptr);
    EXPECT_NE(scene.find("lamp1"), nullptr);
}

TEST(SceneMutations, RebuildFiltersGroupId) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n1 = make_bp2_node(interner, "bat1", "Battery");

    // Create scope host: blueprint-instance node with embedded nested.
    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("group_A");
    host.semantic.type = interner.intern("HostType");

    // Create inner blueprint (empty for this test)
    auto inner_def = std::make_unique<bp2::Blueprint>();
    *inner_def = inner_def->with_id(interner.intern("HostType"));
    *inner_def = inner_def->with_interface(bp2::Interface());
    
    // Attach embedded blueprint as source
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner_def->with_id(interner.intern("HostType")))
    )
    };

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n1));
    bp = bp.with_node(std::move(host));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    // Only the root-level nodes should appear (bat1 + group_A host)
    EXPECT_EQ(scene.roots().size(), 2u);
    EXPECT_NE(scene.find("bat1"), nullptr);
    EXPECT_NE(scene.find("group_A"), nullptr);
}

TEST(SceneMutations, RebuildCreatesWireWidgets) {
    core::StringInterner interner;
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
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    // 2 nodes + 1 wire = 3 roots
    EXPECT_EQ(scene.roots().size(), 3u);
    EXPECT_NE(scene.find("wire_0"), nullptr);
}

TEST(SceneMutations, RebuildClearsExistingScene) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n1 = make_bp2_node(interner, "bat1", "Battery");

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n1));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());
    EXPECT_EQ(scene.roots().size(), 1u);

    // Rebuild again — should clear first
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());
    EXPECT_EQ(scene.roots().size(), 1u);
}

// ============================================================================
// Bus node wire operations
// ============================================================================

TEST(SceneMutations, RebuildWithBusNodeCreatesAliasPortWires) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    // Bus node
    auto bus = make_bp2_node(interner, "bus1", "Bus");
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
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    // 2 nodes + 1 wire
    EXPECT_EQ(scene.roots().size(), 3u);
    EXPECT_NE(scene.find("bus1"), nullptr);
    EXPECT_NE(scene.find("wire_0"), nullptr);
}

// ============================================================================
// REGRESSION: Scene rebuild preserves custom colors from data layer
// ============================================================================

TEST(SceneMutations, RebuildPreservesNodeColor) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n = make_bp2_node(interner, "bat1", "Battery");
    n.view.color = editor::NodeColor{0.8f, 0.2f, 0.1f, 1.0f};

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    auto* w = scene.find("bat1");
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->customColor().has_value());
}

TEST(SceneMutations, RebuildPreservesBusNodeColor) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    auto bus = make_bp2_node(interner, "bus1", "Bus");
    bus.view.color = editor::NodeColor{0.1f, 0.5f, 0.9f, 1.0f};

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bus));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    auto* w = scene.find("bus1");
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->customColor().has_value());
}

TEST(SceneMutations, RebuildNoColorWhenNodeHasNoColor) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    auto n = make_bp2_node(interner, "bat1", "Battery");
    // has_color defaults to false

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    auto* w = scene.find("bat1");
    ASSERT_NE(w, nullptr);
    EXPECT_FALSE(w->customColor().has_value());
}

// ============================================================================
// Multiple wires on bus rebuild correctly
// ============================================================================

TEST(SceneMutations, RebuildMultipleBusWires) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    auto bus = make_bp2_node(interner, "bus1", "Bus");
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
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

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

    core::StringInterner interner;
    bp2::PathArena arena(interner);

    ComponentRegistry parser_registry = load_component_registry("library/");
    auto bp_opt = load_blueprint_from_file(gsc_path.c_str(), interner, arena, parser_registry);
    if (!bp_opt.has_value()) {
        GTEST_SKIP() << "GSC.blueprint present but not decodable under strict schema";
    }

    visual::Scene scene;
    visual::mutations::rebuild(scene, *bp_opt, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    auto* pi_widget_base = scene.find("pi_1");
    ASSERT_NE(pi_widget_base, nullptr);
    auto* pi_widget = dynamic_cast<visual::NodeWidget*>(pi_widget_base);
    ASSERT_NE(pi_widget, nullptr);

    // A known top-level wire in GSC.blueprint should be rendered as a widget.
    // Note: wire_200 is the actual ID in the blueprint (wire_20 was stale).
    EXPECT_NE(scene.find("wire_200"), nullptr);
}

TEST(SceneMutations, RefNodePortCenteredOnNodeWidth) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    auto ref = make_bp2_node(interner, "ref1", "RefNode");
    set_iface(ref, {
        make_port(interner, "v", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(ref));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

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
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    // ref at (0,0), battery at (200,0) → ref should face right
    auto ref = make_bp2_node(interner, "ref1", "RefNode");
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
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(scene.find("ref1"));
    ASSERT_NE(ref_widget, nullptr);

    auto* p = ref_widget->port("v");
    ASSERT_NE(p, nullptr);
    // Port should be on Right edge → port center x near node width
    float port_center_x = p->localPos().x + visual::PortConstants::RADIUS;
    EXPECT_NEAR(port_center_x, ref_widget->size().x, 1e-4f);
}

TEST(SceneMutations, RefNodeOrientsFacingConnectedNode_Left) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    // ref at (200,0), battery at (0,0) → ref should face left
    auto ref = make_bp2_node(interner, "ref1", "RefNode");
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
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(scene.find("ref1"));
    ASSERT_NE(ref_widget, nullptr);

    auto* p = ref_widget->port("v");
    ASSERT_NE(p, nullptr);
    // Port should be on Left edge → port center x near 0
    float port_center_x = p->localPos().x + visual::PortConstants::RADIUS;
    EXPECT_NEAR(port_center_x, 0.0f, 1e-4f);
}

TEST(SceneMutations, RefNodeOrientsFacingConnectedNode_Bottom) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    // ref at (0,0), battery at (0,200) → ref should face bottom
    auto ref = make_bp2_node(interner, "ref1", "RefNode");
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
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(scene.find("ref1"));
    ASSERT_NE(ref_widget, nullptr);

    auto* p = ref_widget->port("v");
    ASSERT_NE(p, nullptr);
    // Port should be on Bottom edge → port center y near node height
    float port_center_y = p->localPos().y + visual::PortConstants::RADIUS;
    EXPECT_NEAR(port_center_y, ref_widget->size().y, 1e-4f);
}

TEST(SceneMutations, RefNodeWithoutWireKeepsDefaultTopOrientation) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    auto ref = make_bp2_node(interner, "ref1", "RefNode");
    set_iface(ref, {
        make_port(interner, "v", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(ref));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(scene.find("ref1"));
    ASSERT_NE(ref_widget, nullptr);

    auto* p = ref_widget->port("v");
    ASSERT_NE(p, nullptr);
    // No wire → stays Top (default). Port center y should be at -RADIUS (top edge).
    float port_center_y = p->localPos().y + visual::PortConstants::RADIUS;
    EXPECT_NEAR(port_center_y, 0.0f, 1e-4f);
}

// ============================================================================
// Dual-grid snap: whole-grid strong, half-grid weak
// ============================================================================

TEST(SnapMath, DualGrid_WholeGridStrong) {
    // Positions near whole-grid lines snap there instantly.
    float grid = 16.0f;

    // On a whole-grid line → stays
    auto r0 = editor_math::snap_to_grid(ui::Pt(0.0f, 0.0f), grid);
    EXPECT_NEAR(r0.x, 0.0f, 1e-4f);
    EXPECT_NEAR(r0.y, 0.0f, 1e-4f);

    // Within strong radius (0.4 * 16 = 6.4) of whole-grid → snaps to whole
    auto r1 = editor_math::snap_to_grid(ui::Pt(5.0f, 3.0f), grid);
    EXPECT_NEAR(r1.x, 0.0f, 1e-4f);   // 5 < 6.4 → whole
    EXPECT_NEAR(r1.y, 0.0f, 1e-4f);   // 3 < 6.4 → whole

    // Just inside strong radius from 16
    auto r2 = editor_math::snap_to_grid(ui::Pt(10.0f, 13.0f), grid);
    EXPECT_NEAR(r2.x, 16.0f, 1e-4f);  // |10-16|=6 ≤ 6.4 → whole
    EXPECT_NEAR(r2.y, 16.0f, 1e-4f);  // |13-16|=3 ≤ 6.4 → whole
}

TEST(SnapMath, DualGrid_HalfGridWeak) {
    // Positions in the narrow corridor at midpoints snap to half-grid.
    float grid = 16.0f;
    float half = 8.0f;

    // Near the half-grid point (8): in the corridor (6.4, 9.6)
    auto r1 = editor_math::snap_to_grid(ui::Pt(7.0f, 7.0f), grid);
    EXPECT_NEAR(r1.x, half, 1e-4f);    // 7 is past strong radius from 0 → half
    EXPECT_NEAR(r1.y, half, 1e-4f);

    auto r2 = editor_math::snap_to_grid(ui::Pt(9.0f, 9.0f), grid);
    EXPECT_NEAR(r2.x, half, 1e-4f);    // 9 is past strong radius from 16 → half
    EXPECT_NEAR(r2.y, half, 1e-4f);

    // Exactly at half-grid point
    auto r3 = editor_math::snap_to_grid(ui::Pt(8.0f, 8.0f), grid);
    EXPECT_NEAR(r3.x, half, 1e-4f);
    EXPECT_NEAR(r3.y, half, 1e-4f);
}

TEST(SnapMath, DualGrid_Boundary) {
    // At the exact boundary between strong and weak zones.
    float grid = 16.0f;
    float strong_r = grid * editor_math::SNAP_STRONG_FRACTION;  // 6.4

    // At strong_r from 0 → snaps to whole (boundary inclusive)
    auto r1 = editor_math::snap_to_grid(ui::Pt(strong_r, 0.0f), grid);
    EXPECT_NEAR(r1.x, 0.0f, 1e-4f);

    // Just past strong_r → snaps to half-grid
    auto r2 = editor_math::snap_to_grid(ui::Pt(strong_r + 0.1f, 0.0f), grid);
    EXPECT_NEAR(r2.x, 8.0f, 1e-4f);
}

TEST(SnapMath, DualGrid_ZeroStep) {
    // Guard: zero grid step returns input unchanged
    auto r = editor_math::snap_to_grid(ui::Pt(7.3f, 2.1f), 0.0f);
    EXPECT_NEAR(r.x, 7.3f, 1e-4f);
    EXPECT_NEAR(r.y, 2.1f, 1e-4f);
}

TEST(SnapMath, DualGrid_WholeGridPointsStable) {
    // Full grid points are always stable (snap to themselves).
    float grid = 16.0f;
    auto r = editor_math::snap_to_grid(ui::Pt(16.0f, 32.0f), grid);
    EXPECT_NEAR(r.x, 16.0f, 1e-4f);
    EXPECT_NEAR(r.y, 32.0f, 1e-4f);
}

TEST(SnapMath, DualGrid_FarFromGridOrigin) {
    // Works correctly far from origin.
    float grid = 10.0f;
    // 103: nearest whole=100, d=3 ≤ 4 (0.4*10) → whole
    auto r = editor_math::snap_to_grid(ui::Pt(103.0f, 103.0f), grid);
    EXPECT_NEAR(r.x, 100.0f, 1e-4f);
    EXPECT_NEAR(r.y, 100.0f, 1e-4f);

    // 107: nearest whole=110, d=3 ≤ 4 → whole
    auto r2 = editor_math::snap_to_grid(ui::Pt(107.0f, 107.0f), grid);
    EXPECT_NEAR(r2.x, 110.0f, 1e-4f);
    EXPECT_NEAR(r2.y, 110.0f, 1e-4f);

    // 105.5: nearest whole=110, d=4.5 > 4 → half at 105
    auto r3 = editor_math::snap_to_grid(ui::Pt(105.5f, 105.5f), grid);
    EXPECT_NEAR(r3.x, 105.0f, 1e-4f);
    EXPECT_NEAR(r3.y, 105.0f, 1e-4f);
}

TEST(SnapMath, DualGrid_NegativeCoordinates) {
    // Negative positions snap correctly (strong zone, weak zone, boundary).
    float grid = 10.0f;

    // -3: nearest whole=0, d=3 ≤ 4 → whole at 0
    auto r1 = editor_math::snap_to_grid(ui::Pt(-3.0f, -3.0f), grid);
    EXPECT_NEAR(r1.x, 0.0f, 1e-4f);
    EXPECT_NEAR(r1.y, 0.0f, 1e-4f);

    // -7: nearest whole=-10, d=3 ≤ 4 → whole at -10
    auto r2 = editor_math::snap_to_grid(ui::Pt(-7.0f, -7.0f), grid);
    EXPECT_NEAR(r2.x, -10.0f, 1e-4f);
    EXPECT_NEAR(r2.y, -10.0f, 1e-4f);

    // -4.5: nearest whole=0, d=4.5 > 4, v < whole → half at -5
    auto r3 = editor_math::snap_to_grid(ui::Pt(-4.5f, -4.5f), grid);
    EXPECT_NEAR(r3.x, -5.0f, 1e-4f);
    EXPECT_NEAR(r3.y, -5.0f, 1e-4f);

    // -5.5: nearest whole=-10, d=4.5 > 4, v > whole → half at -5
    auto r4 = editor_math::snap_to_grid(ui::Pt(-5.5f, -5.5f), grid);
    EXPECT_NEAR(r4.x, -5.0f, 1e-4f);
    EXPECT_NEAR(r4.y, -5.0f, 1e-4f);
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
    core::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_bp2_node(I, "knob_1", "KnobSwitch");
    set_iface(knob, {
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw3", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });
     bp2::Blueprint bp;
     bp = bp.with_node(std::move(knob));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, I, arena, std::span<const core::InternedId>{}, scene_reg());

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
    core::StringInterner I;
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
    visual::mutations::rebuild(scene, bp, I, arena, std::span<const core::InternedId>{}, scene_reg());

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
    core::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_bp2_node(I, "knob_1", "KnobSwitch");
    set_iface(knob, {
        make_port(I, "wiper", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });
     bp2::Blueprint bp;
     bp = bp.with_node(std::move(knob));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, I, arena, std::span<const core::InternedId>{}, scene_reg());

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


TEST(SceneMutations, LongHeaderTextDoesNotInflateMinimumNodeWidth) {
    // Regression: a long type name (e.g. "VariableConductance") used to drive
    // the node minimum width via the FooterTypeLabel, and a long instance name
    // did the same via the HeaderStrip, creating a large empty gap between
    // left and right port labels. After the fix, both header and footer minimum
    // width is zero (text can be clipped), so minimum node width is driven
    // solely by port rows and content.
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    // Short port names with a very long type name.
    auto node = make_bp2_node(interner, "vc1", "VariableConductance");
    set_iface(node, {
        make_port(interner, "a", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "b", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    // Same topology but with a short type name.
    auto node_short = make_bp2_node(interner, "s1", "VCon");
    set_iface(node_short, {
        make_port(interner, "a", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "b", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(node));
    bp = bp.with_node(std::move(node_short));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    auto* w_long  = dynamic_cast<visual::NodeWidget*>(scene.find("vc1"));
    auto* w_short = dynamic_cast<visual::NodeWidget*>(scene.find("s1"));
    ASSERT_NE(w_long, nullptr);
    ASSERT_NE(w_short, nullptr);

    // Both nodes have identical ports, so their minimum width must be equal —
    // the longer type/header name must not inflate the minimum.
    EXPECT_EQ(w_long->minimumNodeSize().x, w_short->minimumNodeSize().x)
        << "Type name length should not affect minimumNodeSize width";
}

TEST(SceneMutations, LongInstanceNameDoesNotInflateMinimumNodeWidth) {
    // Companion to LongHeaderTextDoesNotInflateMinimumNodeWidth:
    // the header (instance name) must not inflate minimum width either.
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    auto node_long = make_bp2_node(interner, "long1", "T");
    node_long.view.name = "VariableConductance_1";
    set_iface(node_long, {
        make_port(interner, "a", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "b", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto node_short = make_bp2_node(interner, "short1", "T");
    node_short.view.name = "X";
    set_iface(node_short, {
        make_port(interner, "a", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "b", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(node_long));
    bp = bp.with_node(std::move(node_short));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    auto* w_long  = dynamic_cast<visual::NodeWidget*>(scene.find("long1"));
    auto* w_short = dynamic_cast<visual::NodeWidget*>(scene.find("short1"));
    ASSERT_NE(w_long, nullptr);
    ASSERT_NE(w_short, nullptr);

    EXPECT_EQ(w_long->minimumNodeSize().x, w_short->minimumNodeSize().x)
        << "Instance name length should not affect minimumNodeSize width";
}

TEST(SceneMutations, IndicatorContentNodeMinimumWidthIgnoresLongPortLabels) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    auto long_labels = make_bp2_node(interner, "light_long", "IndicatorLight");
    long_labels.view.name = "light_long";
    set_iface(long_labels, {
        make_port(interner, "brightness", Domain::Electrical, bp2::Direction::Output, PortType::I),
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto short_labels = make_bp2_node(interner, "light_short", "IndicatorLight");
    short_labels.view.name = "light_short";
    set_iface(short_labels, {
        make_port(interner, "b", Domain::Electrical, bp2::Direction::Output, PortType::I),
        make_port(interner, "a", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "o", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(long_labels));
    bp = bp.with_node(std::move(short_labels));

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, scene_reg());

    auto* long_widget = dynamic_cast<visual::NodeWidget*>(scene.find("light_long"));
    auto* short_widget = dynamic_cast<visual::NodeWidget*>(scene.find("light_short"));
    ASSERT_NE(long_widget, nullptr);
    ASSERT_NE(short_widget, nullptr);

    EXPECT_EQ(long_widget->minimumNodeSize().x, short_widget->minimumNodeSize().x);
}

TEST(SceneMutations, RebuildSeedsWidgetWithLiveDynamicContentState) {
    core::StringInterner interner;
    bp2::PathArena arena(interner);

    ComponentRegistry reg = scene_reg();

    // Register dynamic-content types not in the base scene registry.
    {
        CompositeSpec slider_spec;
        slider_spec.classname = "Slider";
        TypePresentation slider_pres;
        slider_pres.content_type = bp2::NodeContentType::Slider;
        slider_spec.params["min"] = ParamSpec{ParamSchemaType::Float, "-10"};
        slider_spec.params["max"] = ParamSpec{ParamSchemaType::Float, "200"};
        reg.register_type("Slider", slider_spec, slider_pres);
    }

    auto slider = make_bp2_node(interner, "slider_live", "Slider");
    slider.view.name = "slider_live";
    slider.semantic.params[interner.intern("min")] = -10.0f;
    slider.semantic.params[interner.intern("max")] = 200.0f;
    set_iface(slider, {
        make_port(interner, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto sw = make_bp2_node(interner, "switch_live", "Switch");
    sw.view.name = "switch_live";
    set_iface(sw, {
        make_port(interner, "state", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    {
        CompositeSpec switch_spec;
        switch_spec.classname = "Switch";
        TypePresentation switch_pres;
        switch_pres.content_type = bp2::NodeContentType::Switch;
        switch_spec.params["closed"] = ParamSpec{ParamSchemaType::Bool, "false"};
        reg.register_type("Switch", switch_spec, switch_pres);
    }

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));
    bp = bp.with_node(std::move(sw));

    editor::RuntimeNodeStateStore runtime_state;
    runtime_state.emplace(editor::make_node_instance_key(std::span<const core::InternedId>{}, interner.lookup("slider_live")),
                          editor::ScalarNodeRuntimeState{42.0f});
    runtime_state.emplace(editor::make_node_instance_key(std::span<const core::InternedId>{}, interner.lookup("switch_live")),
                          editor::BoolNodeRuntimeState{true});

    visual::Scene scene;
    visual::mutations::rebuild(scene, bp, interner, arena, std::span<const core::InternedId>{}, reg, &runtime_state);

    auto* slider_widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_live"));
    auto* switch_widget = dynamic_cast<visual::NodeWidget*>(scene.find("switch_live"));
    ASSERT_NE(slider_widget, nullptr);
    ASSERT_NE(switch_widget, nullptr);

    NodeContent slider_content = slider_widget->currentContent();
    EXPECT_EQ(slider_content.type, bp2::NodeContentType::Slider);
    EXPECT_FLOAT_EQ(slider_content.min, -10.0f);
    EXPECT_FLOAT_EQ(slider_content.max, 200.0f);
    EXPECT_FLOAT_EQ(slider_content.value, 42.0f)
        << "Rebuild must seed NodeWidget with live slider value, not static default";

    NodeContent switch_content = switch_widget->currentContent();
    EXPECT_EQ(switch_content.type, bp2::NodeContentType::Switch);
    EXPECT_TRUE(switch_content.state)
        << "Rebuild must seed NodeWidget with live switch state, not static default";
}
