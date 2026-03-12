#include <gtest/gtest.h>
#include "editor/visual/hittest.h"
#include "editor/visual/spatial_grid.h"
#include "editor/visual/trigonometry.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/visual/node/node.h"
#include "editor/visual/node/types/bus_node.h"
#include "editor/visual/node/types/ref_node.h"
#include "editor/visual/node/visual_node_cache.h"
#include <cmath>

using an24::BusVisualNode;
using an24::BusOrientation;
using an24::RefVisualNode;
using an24::VisualNodeFactory;

/// TDD Step 6: Hit testing

TEST(HitTest, EmptyBlueprint_ReturnsNone) {
    Blueprint bp;
    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    auto hit = hit_test(bp, cache, Pt(100.0f, 100.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::None);
}

TEST(HitTest, Node_Inside_ReturnsNode) {
    Blueprint bp;
    Node n;
    n.id = "batt1";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    // Клик внутри узла
    auto hit = hit_test(bp, cache, Pt(150.0f, 80.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 0);
}

TEST(HitTest, Node_Outside_ReturnsNone) {
    Blueprint bp;
    Node n;
    n.id = "batt1";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    // Клик вне узла
    auto hit = hit_test(bp, cache, Pt(0.0f, 0.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::None);
}

TEST(HitTest, MultipleNodes_ReturnsClosest) {
    Blueprint bp;

    Node n1;
    n1.id = "n1";
    n1.at(0.0f, 0.0f);
    n1.size_wh(100.0f, 50.0f);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "n2";
    n2.at(200.0f, 0.0f);
    n2.size_wh(100.0f, 50.0f);
    bp.add_node(std::move(n2));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    // Клик между узлами - первый найденный
    auto hit = hit_test(bp, cache, Pt(50.0f, 25.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 0);
}

// ============================================================================
// Port Hit Testing (TDD for wire creation)
// ============================================================================

TEST(PortHitTest, EmptyBlueprint_NoPorts) {
    Blueprint bp;
    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    auto hit = hit_test_ports(bp, cache, Pt(100.0f, 100.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::None);
}

TEST(PortHitTest, NodeWithPorts_ClickNearPort_ReturnsPort) {
    Blueprint bp;
    Node n;
    n.id = "batt1";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    n.input("v_in").output("v_out");
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Создаём визуальный узел
    auto* visual = cache.getOrCreate(bp.nodes[0]);
    ASSERT_NE(visual, nullptr);

    // Получаем позицию входного порта
    Pt port_pos = visual->getPort("v_in")->worldPosition();

    // Кликаем прямо на порт
    auto hit = hit_test_ports(bp, cache, port_pos, "", grid);
    EXPECT_EQ(hit.type, HitType::Port);
    EXPECT_EQ(hit.port_node_id, "batt1");
    EXPECT_EQ(hit.port_name, "v_in");
}

TEST(PortHitTest, ClickFarFromPorts_ReturnsNone) {
    Blueprint bp;
    Node n;
    n.id = "batt1";
    n.at(100.0f, 50.0f);
    n.input("v_in").output("v_out");
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Кликаем далеко от портов
    auto hit = hit_test_ports(bp, cache, Pt(1000.0f, 1000.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::None);
}

// ============================================================================
// PortAlias Tests (TDD)
// ============================================================================

TEST(PortAliasTest, BusWithWire_ShouldCreateMultipleVisualPorts) {
    // Create a Bus with a wire
    Blueprint bp;
    Node bus;
    bus.id = "bus_1";
    bus.render_hint = "bus";
    bus.at(100.0f, 100.0f);
    bus.size_wh(40.0f, 40.0f);
    bus.input("v").output("v");  // InOut port
    bp.add_node(std::move(bus));

    Wire w = Wire::make("wire_1",
        WireEnd("battery_1", "v_out", PortSide::Output),
        WireEnd("bus_1", "v", PortSide::InOut));
    bp.add_wire(std::move(w));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    auto* visual = cache.getOrCreate(bp.nodes[0], bp.wires);
    ASSERT_NE(visual, nullptr);

    // Should have 2 visual ports: "v" + wire port
    EXPECT_EQ(visual->getPortCount(), 2);  // FAILS - currently only 1
}

TEST(PortAliasTest, HitTestOnWirePort_ShouldReturnTargetPortName) {
    // Create a Bus with wire
    Blueprint bp;
    Node bus;
    bus.id = "bus_1";
    bus.render_hint = "bus";
    bus.at(100.0f, 100.0f);
    bus.size_wh(40.0f, 40.0f);
    bus.input("v").output("v");
    bp.add_node(std::move(bus));

    Wire w = Wire::make("wire_1",
        WireEnd("battery_1", "v_out", PortSide::Output),
        WireEnd("bus_1", "v", PortSide::InOut));
    bp.add_wire(std::move(w));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    auto* visual = cache.getOrCreate(bp.nodes[0], bp.wires);
    ASSERT_NE(visual, nullptr);

    // Get position of visual port created for wire
    Pt wire_port_pos = visual->getPort("wire_1")->worldPosition();

    // Hit test should find the port
    auto hit = hit_test_ports(bp, cache, wire_port_pos, "", grid);
    EXPECT_EQ(hit.type, HitType::Port);
    EXPECT_EQ(hit.port_node_id, "bus_1");

    // Should return TARGET port "v", not visual alias name "wire_1"
    // This ensures wire creation uses correct port names
    EXPECT_EQ(hit.port_name, "v");  // FAILS - returns "wire_1"
}

// ============================================================================
// Regression tests for Bus port fixes
// ============================================================================

// [a3f7c1e0] Bus setSize override: external size must not corrupt port positions.
// Root cause: an24_editor.cpp called visual->setSize(node.size) which overrode
// BusVisualNode's internally calculated size (48x32) with node.size (40x40).
// This changed ports_on_bottom from true to false, moving ports from bottom to
// right side. Rendered ports (fresh visuals) were on bottom, but hit-tested
// ports (cached visuals) were on the right — making Bus ports unclickable.
TEST(RegressionBusPort, SetSizeDoesNotCorruptPortPositions) {
    Node bus;
    bus.id = "bus_1";
    bus.render_hint = "bus";
    bus.at(96.0f, 96.0f);
    bus.size_wh(40.0f, 40.0f);  // node.size from add_component
    bus.input("v").output("v");

    BusVisualNode visual(bus);

    // Get port position before setSize
    Pt pos_before = visual.getPort("v")->worldPosition();

    // Simulate what an24_editor.cpp does: setSize with node.size
    visual.setSize(Pt(40.0f, 40.0f));

    // Port position must not change
    Pt pos_after = visual.getPort("v")->worldPosition();
    EXPECT_FLOAT_EQ(pos_before.x, pos_after.x);
    EXPECT_FLOAT_EQ(pos_before.y, pos_after.y);

    // Size must be internally computed, not 40x40
    Pt size = visual.getSize();
    EXPECT_NE(size.x, 40.0f);  // Should be 48 (calculated from port count)
}

// [a3f7c1e0] Verify port positions match between fresh visual and cached visual
// after setSize override. This was the exact scenario causing the hit test miss.
TEST(RegressionBusPort, CachedAndFreshVisualsAgreeOnPortPosition) {
    Blueprint bp;
    Node bus;
    bus.id = "bus_1";
    bus.render_hint = "bus";
    bus.at(96.0f, 96.0f);
    bus.size_wh(40.0f, 40.0f);
    bus.input("v").output("v");
    bp.add_node(bus);

    // Create cached visual (as hit_test_ports does)
    VisualNodeCache cache;
    auto* cached = cache.getOrCreate(bp.nodes[0], bp.wires);
    cached->setPosition(bus.pos);
    cached->setSize(bus.size);  // This was corrupting the size

    // Create fresh visual (as render_blueprint does)
    auto fresh = VisualNodeFactory::create(bp.nodes[0], bp.wires);

    // Both must agree on port position
    Pt cached_pos = cached->getPort("v")->worldPosition();
    Pt fresh_pos = fresh->getPort("v")->worldPosition();
    EXPECT_FLOAT_EQ(cached_pos.x, fresh_pos.x);
    EXPECT_FLOAT_EQ(cached_pos.y, fresh_pos.y);
}

// [a3f7c1e0] Hit test must find Bus "v" port even after setSize override.
TEST(RegressionBusPort, HitTestFindsPortAfterSetSize) {
    Blueprint bp;
    Node bus;
    bus.id = "bus_1";
    bus.render_hint = "bus";
    bus.at(96.0f, 96.0f);
    bus.size_wh(40.0f, 40.0f);
    bus.input("v").output("v");
    bp.add_node(bus);

    VisualNodeCache cache;
    auto* visual = cache.getOrCreate(bp.nodes[0], bp.wires);
    visual->setPosition(bus.pos);
    visual->setSize(bus.size);

    // Get the actual port position (after setSize, which should be no-op)
    Pt port_pos = visual->getPort("v")->worldPosition();

    // Hit test at exact port position must find it
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    auto hit = hit_test_ports(bp, cache, port_pos, "", grid);
    EXPECT_EQ(hit.type, HitType::Port);
    EXPECT_EQ(hit.port_node_id, "bus_1");
    EXPECT_EQ(hit.port_name, "v");
    EXPECT_EQ(hit.port_side, PortSide::InOut);
}

// [c5a9b7d2] RefVisualNode must use actual port name from node definition.
// Previously hardcoded "ref" caused wire creation to use wrong port name.
TEST(RegressionRefPort, RefNodeUsesActualPortName) {
    Node ref;
    ref.id = "gnd_1";
    ref.render_hint = "ref";
    ref.at(96.0f, 96.0f);
    ref.size_wh(40.0f, 40.0f);
    ref.output("v");

    RefVisualNode visual(ref);
    EXPECT_EQ(visual.getPortCount(), 1u);

    const auto* port = visual.getPort(size_t(0));
    ASSERT_NE(port, nullptr);
    EXPECT_EQ(port->name(), "v");  // Was "ref" before fix
}

// [c5a9b7d2] Hit test on RefNode must return "v" not "ref".
TEST(RegressionRefPort, HitTestReturnsCorrectPortName) {
    Blueprint bp;
    Node ref;
    ref.id = "gnd_1";
    ref.render_hint = "ref";
    ref.at(96.0f, 96.0f);
    ref.size_wh(40.0f, 40.0f);
    ref.output("v");
    bp.add_node(ref);

    VisualNodeCache cache;
    auto* visual = cache.getOrCreate(bp.nodes[0], bp.wires);
    Pt port_pos = visual->getPort("v")->worldPosition();

    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    auto hit = hit_test_ports(bp, cache, port_pos, "", grid);
    EXPECT_EQ(hit.type, HitType::Port);
    EXPECT_EQ(hit.port_name, "v");  // Was "ref" before fix
    EXPECT_EQ(hit.port_side, PortSide::Output);
}

// [b8d2e4f1] BusVisualNode::connectWire must update wires_ before redistributing.
TEST(RegressionBusPort, ConnectWireUpdatesWiresList) {
    Node bus;
    bus.id = "bus_1";
    bus.render_hint = "bus";
    bus.at(96.0f, 96.0f);
    bus.input("v").output("v");

    BusVisualNode visual(bus);
    EXPECT_EQ(visual.getPortCount(), 1u);  // Just "v"

    Wire w = Wire::make("wire_1",
        WireEnd("battery_1", "v_out", PortSide::Output),
        WireEnd("bus_1", "v", PortSide::InOut));
    visual.connectWire(w);

    // Should now have 2 ports: "v" + "wire_1"
    EXPECT_EQ(visual.getPortCount(), 2u);
}

// [e7b4c2d5] getPortPosition wire_id fallback must use correct index (1-based for wire ports).
TEST(RegressionBusPort, GetPortPositionWireIdCorrectIndex) {
    Node bus;
    bus.id = "bus_1";
    bus.render_hint = "bus";
    bus.at(96.0f, 96.0f);
    bus.input("v").output("v");

    Wire w = Wire::make("wire_1",
        WireEnd("battery_1", "v_out", PortSide::Output),
        WireEnd("bus_1", "v", PortSide::InOut));

    BusVisualNode visual(bus, BusOrientation::Horizontal, {w});

    // Port "v" is at index 0, wire alias is at index 1
    Pt v_pos = visual.getPort("v")->worldPosition();
    Pt wire_pos = visual.resolveWirePort("v", "wire_1")->worldPosition();

    // They must be at different positions (off-by-one bug made them the same)
    EXPECT_NE(v_pos.x, wire_pos.x);
}

// ============================================================================
// [h1a2b3c4] Cached hit_test overload must give same results as uncached
// ============================================================================

TEST(CachedHitTest, CachedHitTest_InsideAndOutside) {
    Blueprint bp;
    Node n;
    n.id = "n1";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Point inside node
    HitResult r1 = hit_test(bp, cache, Pt(150.0f, 90.0f), "", grid);
    EXPECT_EQ(r1.type, HitType::Node);

    // Point outside
    HitResult r2 = hit_test(bp, cache, Pt(500.0f, 500.0f), "", grid);
    EXPECT_EQ(r2.type, HitType::None);
}

// [i2d4e6f8] Wire hit tolerance should be uniform (5.0f)
TEST(WireHitTolerance, UniformToleranceForAllSegments) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.at(0, 0); n1.size_wh(120, 80); n1.output("o");
    Node n2; n2.id = "b"; n2.at(400, 0); n2.size_wh(120, 80); n2.input("i");
    bp.add_node(std::move(n1));
    bp.add_node(std::move(n2));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    w.add_routing_point(Pt(200.0f, 100.0f));
    bp.add_wire(std::move(w));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // 4.5 world units from the first segment (start→routing_point) should hit
    // This confirms tolerance is 5.0f, not 20.0f
    Pt start_pos = editor_math::get_port_position(bp.nodes[0], "o", bp.wires, nullptr, cache);
    // Midpoint of first segment + 4.5 units perpendicular
    Pt mid((start_pos.x + 200.0f) / 2.0f, (start_pos.y + 100.0f) / 2.0f);
    float seg_dx = 200.0f - start_pos.x;
    float seg_dy = 100.0f - start_pos.y;
    float seg_len = std::sqrt(seg_dx * seg_dx + seg_dy * seg_dy);
    Pt perp(-seg_dy / seg_len * 4.5f, seg_dx / seg_len * 4.5f);
    Pt test_pt(mid.x + perp.x, mid.y + perp.y);

    HitResult r = hit_test(bp, cache, test_pt, "", grid);
    EXPECT_EQ(r.type, HitType::Wire);

    // 6.0 units away should NOT hit
    Pt perp_far(-seg_dy / seg_len * 6.0f, seg_dx / seg_len * 6.0f);
    Pt test_pt_far(mid.x + perp_far.x, mid.y + perp_far.y);
    HitResult r2 = hit_test(bp, cache, test_pt_far, "", grid);
    EXPECT_EQ(r2.type, HitType::None);
}

// ============================================================================
// [p1q2r3s4] Regression: wire click on Bus-connected wires
// Wire hit-test must pass wire_id to get_port_position so Bus alias ports
// resolve correctly. Previously nullptr caused all endpoints to collapse
// to the main "v" port → missed clicks.
// ============================================================================

TEST(HitTest, WireClick_BusTwoWires_CachedOverload) {
    Blueprint bp;

    // Bus node with logical port "v"
    Node bus; bus.id = "bus1"; bus.name = "bus"; bus.render_hint = "bus";
    bus.at(200, 100).size_wh(80, 32);
    bus.input("v"); bus.output("v");
    bp.add_node(std::move(bus));

    // Two standard nodes on left and right
    Node n1; n1.id = "a"; n1.name = "A"; n1.at(0, 0); n1.size_wh(120, 80);
    n1.output("o");
    Node n2; n2.id = "b"; n2.name = "B"; n2.at(0, 200); n2.size_wh(120, 80);
    n2.output("o");
    bp.add_node(std::move(n1));
    bp.add_node(std::move(n2));

    // Two wires from different nodes to the same Bus "v" port
    Wire w1 = Wire::make("w1", wire_output("a", "o"), wire_input("bus1", "v"));
    Wire w2 = Wire::make("w2", wire_output("b", "o"), wire_input("bus1", "v"));
    bp.add_wire(std::move(w1));
    bp.add_wire(std::move(w2));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Build visuals so Bus has two alias ports
    for (auto& n : bp.nodes)
        cache.getOrCreate(n, bp.wires);

    // Click near the midpoint of wire w2
    Pt start2 = editor_math::get_port_position(bp.nodes[2], "o", bp.wires, "w2", cache);
    Pt end2   = editor_math::get_port_position(bp.nodes[0], "v", bp.wires, "w2", cache);
    Pt mid2((start2.x + end2.x) / 2.0f, (start2.y + end2.y) / 2.0f);

    HitResult r = hit_test(bp, cache, mid2, "", grid);
    EXPECT_EQ(r.type, HitType::Wire);
    EXPECT_EQ(r.wire_index, 1u);  // w2 is the second wire
}

// ============================================================================
// [g1h2i3j4] Regression: hit_test_ports returns wire_id for Bus alias ports
// ============================================================================

TEST(HitTest, PortHit_BusAlias_ReturnsCorrectWireId) {
    Blueprint bp;

    // Bus node with two wires connected
    Node bus; bus.id = "bus1"; bus.name = "bus"; bus.render_hint = "bus";
    bus.at(200, 100).size_wh(80, 32);
    bus.input("v"); bus.output("v");
    bp.add_node(std::move(bus));

    Node n1; n1.id = "a"; n1.name = "A"; n1.at(0, 0); n1.size_wh(120, 80);
    n1.output("o");
    Node n2; n2.id = "b"; n2.name = "B"; n2.at(0, 200); n2.size_wh(120, 80);
    n2.output("o");
    bp.add_node(std::move(n1));
    bp.add_node(std::move(n2));

    Wire w1 = Wire::make("w1", wire_output("a", "o"), wire_input("bus1", "v"));
    Wire w2 = Wire::make("w2", wire_output("b", "o"), wire_input("bus1", "v"));
    bp.add_wire(std::move(w1));
    bp.add_wire(std::move(w2));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    for (auto& n : bp.nodes)
        cache.getOrCreate(n, bp.wires);

    // Get position of w2's alias port on the bus
    Pt w2_port_pos = editor_math::get_port_position(bp.nodes[0], "v", bp.wires, "w2", cache);

    HitResult r = hit_test_ports(bp, cache, w2_port_pos, "", grid);
    EXPECT_EQ(r.type, HitType::Port);
    EXPECT_EQ(r.port_node_id, "bus1");
    EXPECT_EQ(r.port_name, "v");        // logical port name
    EXPECT_EQ(r.port_wire_id, "w2");    // alias wire ID
}

TEST(HitTest, PortHit_BusMainV_EmptyWireId) {
    Blueprint bp;

    Node bus; bus.id = "bus1"; bus.name = "bus"; bus.render_hint = "bus";
    bus.at(200, 100).size_wh(80, 32);
    bus.input("v"); bus.output("v");
    bp.add_node(std::move(bus));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    cache.getOrCreate(bp.nodes[0], bp.wires);

    // Main "v" port (no alias) — wire_id should be empty
    auto* visual = cache.get("bus1");
    ASSERT_NE(visual, nullptr);
    // "v" is the only port (no wires), at index 0
    Pt v_pos = visual->getPort("v")->worldPosition();

    HitResult r = hit_test_ports(bp, cache, v_pos, "", grid);
    EXPECT_EQ(r.type, HitType::Port);
    EXPECT_EQ(r.port_name, "v");
    EXPECT_TRUE(r.port_wire_id.empty());  // main "v" port, not an alias
}

// =============================================================================
// Group Filtering Tests: Blueprint Collapsing via group_id
// =============================================================================

TEST(HitTestGroupFilter, NodeInDifferentGroup_NotHittable) {
    Blueprint bp;

    Node n;
    n.id = "internal1";
    n.at(100.0f, 50.0f).size_wh(120.0f, 80.0f);
    n.group_id = "lamp1";  // Not in root group
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    // Click inside the node's bounds, but filtering for root group ""
    auto hit = hit_test(bp, cache, Pt(150.0f, 80.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::None) << "Node in different group should not be hittable";
}

TEST(HitTestGroupFilter, NodeInDifferentGroup_NotHittable_WithCache) {
    Blueprint bp;

    Node n;
    n.id = "internal1";
    n.at(100.0f, 50.0f).size_wh(120.0f, 80.0f);
    n.group_id = "lamp1";
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    auto hit = hit_test(bp, cache, Pt(150.0f, 80.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::None) << "Node in different group should not be hittable";
}

TEST(HitTestGroupFilter, NodeInSameGroup_Hittable) {
    Blueprint bp;

    Node n;
    n.id = "root1";
    n.at(100.0f, 50.0f).size_wh(120.0f, 80.0f);
    n.group_id = "";
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    auto hit = hit_test(bp, cache, Pt(150.0f, 80.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::Node) << "Node in same group should be hittable";
    EXPECT_EQ(hit.node_index, 0u);
}

TEST(HitTestGroupFilter, PortInDifferentGroup_NotHittable) {
    Blueprint bp;

    Node n;
    n.id = "internal1";
    n.at(100.0f, 50.0f).size_wh(120.0f, 80.0f);
    n.group_id = "lamp1";
    n.input("v_in").output("v_out");
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    auto* visual = cache.getOrCreate(bp.nodes[0], bp.wires);
    ASSERT_NE(visual, nullptr);

    Pt port_pos = visual->getPort("v_in")->worldPosition();

    auto hit = hit_test_ports(bp, cache, port_pos, "", grid);
    EXPECT_EQ(hit.type, HitType::None) << "Port on node in different group should not be hittable";
}

TEST(HitTestGroupFilter, WireCrossGroup_NotHittable) {
    Blueprint bp;

    Node n1;
    n1.id = "n1";
    n1.at(0.0f, 0.0f).size_wh(100.0f, 50.0f);
    n1.group_id = "";
    n1.output("out");
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "n2";
    n2.at(300.0f, 0.0f).size_wh(100.0f, 50.0f);
    n2.group_id = "lamp1";
    n2.input("in");
    bp.add_node(std::move(n2));

    Wire w;
    w.id = "w1";
    w.start.node_id = "n1";
    w.start.port_name = "out";
    w.end.node_id = "n2";
    w.end.port_name = "in";
    bp.add_wire(std::move(w));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");
    auto hit = hit_test(bp, cache, Pt(200.0f, 25.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::None) << "Wire crossing groups should not be hittable";
}

TEST(HitTestGroupFilter, DrillInOut_GroupSwitching) {
    Blueprint bp;

    // Collapsed node - root group
    Node collapsed;
    collapsed.id = "lamp1";
    collapsed.at(100.0f, 50.0f).size_wh(120.0f, 80.0f);
    collapsed.group_id = "";
    collapsed.expandable = true;
    bp.add_node(std::move(collapsed));

    // Internal node - in "lamp1" group
    Node internal;
    internal.id = "lamp1:lamp";
    internal.at(100.0f, 50.0f).size_wh(120.0f, 80.0f);
    internal.group_id = "lamp1";
    bp.add_node(std::move(internal));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // At root: collapsed is hittable, internal is not
    auto hit = hit_test(bp, cache, Pt(150.0f, 80.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 0u);

    // Drilled into "lamp1": internal is hittable, collapsed is not
    grid.rebuild(bp, cache, "lamp1");
    hit = hit_test(bp, cache, Pt(150.0f, 80.0f), "lamp1", grid);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 1u) << "After drill-in, internal node should be hittable";
}

// ============================================================================
// Regression: wire hit test near routing points (L-turns)
// The routing point hit radius (10px) is larger than wire segment tolerance (5px).
// Points on a wire segment near a routing point must still return Wire or
// RoutingPoint — never None.
// ============================================================================

TEST(HitTest, WireHittableNearRoutingPoint) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.at(0, 0).size_wh(120, 80); n1.output("o");
    Node n2; n2.id = "b"; n2.at(400, 200).size_wh(120, 80); n2.input("i");
    bp.add_node(std::move(n1));
    bp.add_node(std::move(n2));

    // L-shaped wire with a routing point at the corner
    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    const Pt rp(300.0f, 40.0f);
    w.add_routing_point(rp);
    bp.add_wire(std::move(w));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Exactly on the routing point → RoutingPoint
    HitResult hit = hit_test(bp, cache, rp, "", grid);
    EXPECT_EQ(hit.type, HitType::RoutingPoint);
    EXPECT_EQ(hit.wire_index, 0u);

    // 8px back along the first segment (within RP radius but ON the wire)
    Pt start_pos = editor_math::get_port_position(bp.nodes[0], "o", bp.wires, nullptr, cache);
    float dx = rp.x - start_pos.x;
    float dy = rp.y - start_pos.y;
    float len = std::sqrt(dx * dx + dy * dy);
    Pt near_rp_on_seg1(rp.x - dx / len * 8.0f, rp.y - dy / len * 8.0f);

    hit = hit_test(bp, cache, near_rp_on_seg1, "", grid);
    EXPECT_TRUE(hit.type == HitType::Wire || hit.type == HitType::RoutingPoint)
        << "Point on segment near RP must be hittable (Wire or RoutingPoint), got None";

    // 8px along the second segment from the RP
    Pt end_pos = editor_math::get_port_position(bp.nodes[1], "i", bp.wires, nullptr, cache);
    float dx2 = end_pos.x - rp.x;
    float dy2 = end_pos.y - rp.y;
    float len2 = std::sqrt(dx2 * dx2 + dy2 * dy2);
    Pt near_rp_on_seg2(rp.x + dx2 / len2 * 8.0f, rp.y + dy2 / len2 * 8.0f);

    hit = hit_test(bp, cache, near_rp_on_seg2, "", grid);
    EXPECT_TRUE(hit.type == HitType::Wire || hit.type == HitType::RoutingPoint)
        << "Point on segment near RP must be hittable (Wire or RoutingPoint), got None";
}

// ============================================================================
// GroupVisualNode hit tests
// ============================================================================

TEST(HitTest, GroupNode_TitleBar_ReturnsNode) {
    Blueprint bp;
    Node g;
    g.id = "grp1";
    g.name = "Power Section";
    g.render_hint = "group";
    g.at(96.0f, 96.0f);
    g.size_wh(192.0f, 128.0f);
    bp.add_node(std::move(g));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Click on title bar (top-left area of the group)
    auto hit = hit_test(bp, cache, Pt(192.0f, 106.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 0u);
}

TEST(HitTest, GroupNode_ResizeHandle_BottomRight) {
    Blueprint bp;
    Node g;
    g.id = "grp1";
    g.name = "Grp";
    g.render_hint = "group";
    g.at(96.0f, 96.0f);
    g.size_wh(192.0f, 128.0f);  // multiples of 16 (PORT_LAYOUT_GRID)
    bp.add_node(std::move(g));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Bottom-right corner at (96+192, 96+128) = (288, 224)
    auto hit = hit_test(bp, cache, Pt(288.0f, 224.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::ResizeHandle);
    EXPECT_EQ(hit.node_index, 0u);
    EXPECT_EQ(hit.resize_corner, ResizeCorner::BottomRight);
}

TEST(HitTest, GroupNode_ResizeHandle_TopLeft) {
    Blueprint bp;
    Node g;
    g.id = "grp1";
    g.render_hint = "group";
    g.at(96.0f, 96.0f);
    g.size_wh(192.0f, 128.0f);
    bp.add_node(std::move(g));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    auto hit = hit_test(bp, cache, Pt(96.0f, 96.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::ResizeHandle);
    EXPECT_EQ(hit.resize_corner, ResizeCorner::TopLeft);
}

TEST(HitTest, NormalNode_NoResizeHandle) {
    Blueprint bp;
    Node n;
    n.id = "bat1";
    n.name = "Bat";
    n.type_name = "Battery";
    n.at(100.0f, 100.0f);
    n.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Click exactly on the corner of a normal node — should be Node, not ResizeHandle
    auto hit = hit_test(bp, cache, Pt(100.0f, 100.0f), "", grid);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_NE(hit.type, HitType::ResizeHandle);
}

// ============================================================================
// GroupVisualNode property tests
// ============================================================================

TEST(GroupVisualNode, IsGroup) {
    Node g;
    g.id = "grp1";
    g.render_hint = "group";
    g.at(0, 0);
    g.size_wh(200.0f, 120.0f);
    auto visual = VisualNodeFactory::create(g);
    EXPECT_EQ(visual->renderLayer(), RenderLayer::Group);
    EXPECT_TRUE(visual->isResizable());
}

TEST(GroupVisualNode, NormalNode_IsNotGroup) {
    Node n;
    n.id = "bat1";
    n.name = "Bat";
    n.type_name = "Battery";
    n.at(0, 0);
    n.size_wh(120.0f, 80.0f);
    auto visual = VisualNodeFactory::create(n);
    EXPECT_EQ(visual->renderLayer(), RenderLayer::Node);
    EXPECT_FALSE(visual->isResizable());
}

TEST(GroupVisualNode, NoPorts) {
    Node g;
    g.id = "grp1";
    g.render_hint = "group";
    g.at(0, 0);
    g.size_wh(200.0f, 120.0f);
    auto visual = VisualNodeFactory::create(g);
    EXPECT_EQ(visual->getPortCount(), 0u);
}

TEST(GroupVisualNode, SizeFromData) {
    Node g;
    g.id = "grp1";
    g.render_hint = "group";
    g.at(48.0f, 64.0f);
    g.size_wh(192.0f, 128.0f);
    auto visual = VisualNodeFactory::create(g);
    EXPECT_FLOAT_EQ(visual->getPosition().x, 48.0f);
    EXPECT_FLOAT_EQ(visual->getPosition().y, 64.0f);
    EXPECT_FLOAT_EQ(visual->getSize().x, 192.0f);
    EXPECT_FLOAT_EQ(visual->getSize().y, 128.0f);
}

// ============================================================================
// Group hit-test: border-only containsPoint (interior passes through)
// ============================================================================

// Helper: build a blueprint with a group covering a wire+node inside it
static Blueprint make_group_with_wire_inside() {
    Blueprint bp;

    // Group at (0,0) size 320x240
    Node g;
    g.id = "grp1";
    g.name = "Power";
    g.render_hint = "group";
    g.at(0.0f, 0.0f);
    g.size_wh(320.0f, 240.0f);
    bp.add_node(std::move(g));

    // Two normal nodes inside the group bounds
    Node n1;
    n1.id = "bat";
    n1.type_name = "Battery";
    n1.output("v_out");
    n1.at(32.0f, 48.0f);
    n1.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "res";
    n2.type_name = "Resistor";
    n2.input("v_in");
    n2.at(192.0f, 48.0f);
    n2.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n2));

    // Wire between them (runs through group interior)
    Wire w = Wire::make("w1", wire_output("bat", "v_out"), wire_input("res", "v_in"));
    bp.add_wire(std::move(w));

    return bp;
}

TEST(GroupHitTest, ContainsPoint_TitleBar_Hit) {
    Node g;
    g.id = "grp1";
    g.name = "Power";
    g.render_hint = "group";
    g.at(0.0f, 0.0f);
    g.size_wh(320.0f, 240.0f);
    auto visual = VisualNodeFactory::create(g);

    // Title bar area (y within top padding+font+padding)
    EXPECT_TRUE(visual->containsPoint(Pt(160.0f, 5.0f)))
        << "Click on title bar should hit group";
    EXPECT_TRUE(visual->containsPoint(Pt(160.0f, 20.0f)))
        << "Click within title height should hit group";
}

TEST(GroupHitTest, ContainsPoint_LeftBorder_Hit) {
    Node g;
    g.id = "grp1";
    g.render_hint = "group";
    g.at(0.0f, 0.0f);
    g.size_wh(320.0f, 240.0f);
    auto visual = VisualNodeFactory::create(g);

    // Near left edge
    EXPECT_TRUE(visual->containsPoint(Pt(3.0f, 120.0f)))
        << "Click near left border should hit group";
}

TEST(GroupHitTest, ContainsPoint_RightBorder_Hit) {
    Node g;
    g.id = "grp1";
    g.render_hint = "group";
    g.at(0.0f, 0.0f);
    g.size_wh(320.0f, 240.0f);
    auto visual = VisualNodeFactory::create(g);

    // Near right edge
    EXPECT_TRUE(visual->containsPoint(Pt(317.0f, 120.0f)))
        << "Click near right border should hit group";
}

TEST(GroupHitTest, ContainsPoint_BottomBorder_Hit) {
    Node g;
    g.id = "grp1";
    g.render_hint = "group";
    g.at(0.0f, 0.0f);
    g.size_wh(320.0f, 240.0f);
    auto visual = VisualNodeFactory::create(g);

    // Near bottom edge
    EXPECT_TRUE(visual->containsPoint(Pt(160.0f, 237.0f)))
        << "Click near bottom border should hit group";
}

TEST(GroupHitTest, ContainsPoint_Interior_PassThrough) {
    Node g;
    g.id = "grp1";
    g.render_hint = "group";
    g.at(0.0f, 0.0f);
    g.size_wh(320.0f, 240.0f);
    auto visual = VisualNodeFactory::create(g);

    // Center of group (well inside borders)
    EXPECT_FALSE(visual->containsPoint(Pt(160.0f, 120.0f)))
        << "Click in group interior should PASS THROUGH (not hit group)";
}

TEST(GroupHitTest, ContainsPoint_Outside_Miss) {
    Node g;
    g.id = "grp1";
    g.render_hint = "group";
    g.at(0.0f, 0.0f);
    g.size_wh(320.0f, 240.0f);
    auto visual = VisualNodeFactory::create(g);

    EXPECT_FALSE(visual->containsPoint(Pt(-10.0f, 120.0f)))
        << "Click outside should miss";
    EXPECT_FALSE(visual->containsPoint(Pt(400.0f, 120.0f)))
        << "Click outside should miss";
}

TEST(GroupHitTest, WireInsideGroup_Hittable) {
    Blueprint bp = make_group_with_wire_inside();

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Get actual wire endpoint positions to find a point on the wire
    const auto* bat = bp.find_node("bat");
    const auto* res = bp.find_node("res");
    ASSERT_NE(bat, nullptr);
    ASSERT_NE(res, nullptr);

    auto* vbat = cache.getOrCreate(*bat, bp.wires);
    auto* vres = cache.getOrCreate(*res, bp.wires);
    const VisualPort* vout = vbat->getPort("v_out");
    const VisualPort* vin  = vres->getPort("v_in");
    ASSERT_NE(vout, nullptr);
    ASSERT_NE(vin, nullptr);

    Pt p1 = vout->worldPosition();
    Pt p2 = vin->worldPosition();
    Pt mid((p1.x + p2.x) / 2, (p1.y + p2.y) / 2);

    auto hit = hit_test(bp, cache, mid, "", grid);
    // Should hit wire inside the group — NOT the group itself
    EXPECT_EQ(hit.type, HitType::Wire)
        << "Wire inside group bounds should be hittable (mid=" << mid.x << "," << mid.y << ")";
}

TEST(GroupHitTest, NodeInsideGroup_Hittable) {
    Blueprint bp = make_group_with_wire_inside();

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Click on center of battery node (32+60=92, 48+40=88)
    Pt bat_center(92.0f, 88.0f);

    auto hit = hit_test(bp, cache, bat_center, "", grid);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 1u)  // bat is index 1 (group is 0)
        << "Should hit battery node inside group, not the group itself";
}

TEST(GroupHitTest, PortInsideGroup_Hittable) {
    Blueprint bp = make_group_with_wire_inside();

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Get battery output port position
    const auto* bat = bp.find_node("bat");
    auto* vbat = cache.getOrCreate(*bat, bp.wires);
    const VisualPort* vout = vbat->getPort("v_out");

    if (vout) {
        Pt port_pos = vout->worldPosition();
        auto hit = hit_test_ports(bp, cache, port_pos, "", grid);
        EXPECT_EQ(hit.type, HitType::Port)
            << "Port inside group bounds should be hittable";
    }
}

TEST(GroupHitTest, GroupTitleBar_HitsGroup) {
    Blueprint bp = make_group_with_wire_inside();

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Click on top-left of group (title bar area)
    Pt title(100.0f, 10.0f);

    auto hit = hit_test(bp, cache, title, "", grid);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 0u)
        << "Clicking on group title bar should select the group";
}

TEST(GroupHitTest, GroupBorder_HitsGroup) {
    Blueprint bp = make_group_with_wire_inside();

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Click on left border of group (x ≈ 2, y in middle)
    Pt left_border(2.0f, 140.0f);

    auto hit = hit_test(bp, cache, left_border, "", grid);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 0u)
        << "Clicking on group border should select the group";
}

// ============================================================================
// TextVisualNode hit-test
// ============================================================================

static Node make_text_node_for_hit(float x = 0, float y = 0,
                                    float w = 192, float h = 128) {
    Node n;
    n.id = "txt1";
    n.type_name = "Text";
    n.render_hint = "text";
    n.at(x, y);
    n.size_wh(w, h);
    n.params["text"] = "Hello";
    return n;
}

TEST(TextHitTest, ContainsPoint_LeftBorder_Hit) {
    auto visual = VisualNodeFactory::create(make_text_node_for_hit());
    visual->setPosition(Pt(0, 0));
    // Left border: x near 0, y in middle
    EXPECT_TRUE(visual->containsPoint(Pt(3.0f, 60.0f)));
}

TEST(TextHitTest, ContainsPoint_RightBorder_Hit) {
    auto visual = VisualNodeFactory::create(make_text_node_for_hit());
    visual->setPosition(Pt(0, 0));
    // Right border: x near 192, y in middle
    EXPECT_TRUE(visual->containsPoint(Pt(189.0f, 60.0f)));
}

TEST(TextHitTest, ContainsPoint_TopBorder_Hit) {
    auto visual = VisualNodeFactory::create(make_text_node_for_hit());
    visual->setPosition(Pt(0, 0));
    // Top border: y near 0
    EXPECT_TRUE(visual->containsPoint(Pt(100.0f, 3.0f)));
}

TEST(TextHitTest, ContainsPoint_BottomBorder_Hit) {
    auto visual = VisualNodeFactory::create(make_text_node_for_hit());
    visual->setPosition(Pt(0, 0));
    // Bottom border: y near 128
    EXPECT_TRUE(visual->containsPoint(Pt(100.0f, 125.0f)));
}

TEST(TextHitTest, ContainsPoint_Interior_Hit) {
    auto visual = VisualNodeFactory::create(make_text_node_for_hit());
    visual->setPosition(Pt(0, 0));
    // Interior center — text nodes are fully clickable
    EXPECT_TRUE(visual->containsPoint(Pt(100.0f, 60.0f)))
        << "Interior of TextVisualNode should be clickable";
}

TEST(TextHitTest, ContainsPoint_Outside_Miss) {
    auto visual = VisualNodeFactory::create(make_text_node_for_hit());
    visual->setPosition(Pt(0, 0));
    EXPECT_FALSE(visual->containsPoint(Pt(-10.0f, 60.0f)));
    EXPECT_FALSE(visual->containsPoint(Pt(100.0f, -10.0f)));
    EXPECT_FALSE(visual->containsPoint(Pt(250.0f, 60.0f)));
    EXPECT_FALSE(visual->containsPoint(Pt(100.0f, 200.0f)));
}

