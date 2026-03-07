#include <gtest/gtest.h>
#include "editor/hittest.h"
#include "editor/trigonometry.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/viewport.h"
#include "editor/visual_node.h"
#include <cmath>

/// TDD Step 6: Hit testing

TEST(HitTest, EmptyBlueprint_ReturnsNone) {
    Blueprint bp;
    Viewport vp;
    auto hit = hit_test(bp, Pt(100.0f, 100.0f), vp);
    EXPECT_EQ(hit.type, HitType::None);
}

TEST(HitTest, Node_Inside_ReturnsNode) {
    Blueprint bp;
    Node n;
    n.id = "batt1";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n));

    Viewport vp;
    // Клик внутри узла
    auto hit = hit_test(bp, Pt(150.0f, 80.0f), vp);
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

    Viewport vp;
    // Клик вне узла
    auto hit = hit_test(bp, Pt(0.0f, 0.0f), vp);
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

    Viewport vp;
    // Клик между узлами - первый найденный
    auto hit = hit_test(bp, Pt(50.0f, 25.0f), vp);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 0);
}

// ============================================================================
// Port Hit Testing (TDD for wire creation)
// ============================================================================

TEST(PortHitTest, EmptyBlueprint_NoPorts) {
    Blueprint bp;
    VisualNodeCache cache;
    Viewport vp;

    auto hit = hit_test_ports(bp, cache, Pt(100.0f, 100.0f));
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
    Viewport vp;

    // Создаём визуальный узел
    auto* visual = cache.getOrCreate(bp.nodes[0]);
    ASSERT_NE(visual, nullptr);

    // Получаем позицию входного порта
    Pt port_pos = visual->getPort("v_in")->worldPosition();

    // Кликаем прямо на порт
    auto hit = hit_test_ports(bp, cache, port_pos);
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
    Viewport vp;

    // Кликаем далеко от портов
    auto hit = hit_test_ports(bp, cache, Pt(1000.0f, 1000.0f));
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
    bus.kind = NodeKind::Bus;
    bus.at(100.0f, 100.0f);
    bus.size_wh(40.0f, 40.0f);
    bus.input("v").output("v");  // InOut port
    bp.add_node(std::move(bus));

    Wire w = Wire::make("wire_1",
        WireEnd("battery_1", "v_out", PortSide::Output),
        WireEnd("bus_1", "v", PortSide::InOut));
    bp.add_wire(std::move(w));

    VisualNodeCache cache;
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
    bus.kind = NodeKind::Bus;
    bus.at(100.0f, 100.0f);
    bus.size_wh(40.0f, 40.0f);
    bus.input("v").output("v");
    bp.add_node(std::move(bus));

    Wire w = Wire::make("wire_1",
        WireEnd("battery_1", "v_out", PortSide::Output),
        WireEnd("bus_1", "v", PortSide::InOut));
    bp.add_wire(std::move(w));

    VisualNodeCache cache;
    auto* visual = cache.getOrCreate(bp.nodes[0], bp.wires);
    ASSERT_NE(visual, nullptr);

    // Get position of visual port created for wire
    Pt wire_port_pos = visual->getPort("wire_1")->worldPosition();

    // Hit test should find the port
    auto hit = hit_test_ports(bp, cache, wire_port_pos);
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
    bus.kind = NodeKind::Bus;
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
    bus.kind = NodeKind::Bus;
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
    bus.kind = NodeKind::Bus;
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
    auto hit = hit_test_ports(bp, cache, port_pos);
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
    ref.kind = NodeKind::Ref;
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
    ref.kind = NodeKind::Ref;
    ref.at(96.0f, 96.0f);
    ref.size_wh(40.0f, 40.0f);
    ref.output("v");
    bp.add_node(ref);

    VisualNodeCache cache;
    auto* visual = cache.getOrCreate(bp.nodes[0], bp.wires);
    Pt port_pos = visual->getPort("v")->worldPosition();

    auto hit = hit_test_ports(bp, cache, port_pos);
    EXPECT_EQ(hit.type, HitType::Port);
    EXPECT_EQ(hit.port_name, "v");  // Was "ref" before fix
    EXPECT_EQ(hit.port_side, PortSide::Output);
}

// [b8d2e4f1] BusVisualNode::connectWire must update wires_ before redistributing.
TEST(RegressionBusPort, ConnectWireUpdatesWiresList) {
    Node bus;
    bus.id = "bus_1";
    bus.kind = NodeKind::Bus;
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
    bus.kind = NodeKind::Bus;
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

TEST(CachedHitTest, CachedHitTestMatchesUncached) {
    Blueprint bp;
    Node n;
    n.id = "n1";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n));

    Viewport vp;
    VisualNodeCache cache;

    // Point inside node
    HitResult r1 = hit_test(bp, Pt(150.0f, 90.0f), vp);
    HitResult r2 = hit_test(bp, cache, Pt(150.0f, 90.0f), vp);
    EXPECT_EQ(r1.type, HitType::Node);
    EXPECT_EQ(r2.type, HitType::Node);
    EXPECT_EQ(r1.node_index, r2.node_index);

    // Point outside
    HitResult r3 = hit_test(bp, Pt(500.0f, 500.0f), vp);
    HitResult r4 = hit_test(bp, cache, Pt(500.0f, 500.0f), vp);
    EXPECT_EQ(r3.type, HitType::None);
    EXPECT_EQ(r4.type, HitType::None);
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

    Viewport vp;

    // 4.5 world units from the first segment (start→routing_point) should hit
    // This confirms tolerance is 5.0f, not 20.0f
    Pt start_pos = editor_math::get_port_position(bp.nodes[0], "o", bp.wires);
    // Midpoint of first segment + 4.5 units perpendicular
    Pt mid((start_pos.x + 200.0f) / 2.0f, (start_pos.y + 100.0f) / 2.0f);
    float seg_dx = 200.0f - start_pos.x;
    float seg_dy = 100.0f - start_pos.y;
    float seg_len = std::sqrt(seg_dx * seg_dx + seg_dy * seg_dy);
    Pt perp(-seg_dy / seg_len * 4.5f, seg_dx / seg_len * 4.5f);
    Pt test_pt(mid.x + perp.x, mid.y + perp.y);

    HitResult r = hit_test(bp, test_pt, vp);
    EXPECT_EQ(r.type, HitType::Wire);

    // 6.0 units away should NOT hit
    Pt perp_far(-seg_dy / seg_len * 6.0f, seg_dx / seg_len * 6.0f);
    Pt test_pt_far(mid.x + perp_far.x, mid.y + perp_far.y);
    HitResult r2 = hit_test(bp, test_pt_far, vp);
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
    Node bus; bus.id = "bus1"; bus.name = "bus"; bus.kind = NodeKind::Bus;
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
    Viewport vp;

    // Build visuals so Bus has two alias ports
    for (auto& n : bp.nodes)
        cache.getOrCreate(n, bp.wires);

    // Click near the midpoint of wire w2
    Pt start2 = editor_math::get_port_position(bp.nodes[2], "o", bp.wires, "w2", &cache);
    Pt end2   = editor_math::get_port_position(bp.nodes[0], "v", bp.wires, "w2", &cache);
    Pt mid2((start2.x + end2.x) / 2.0f, (start2.y + end2.y) / 2.0f);

    HitResult r = hit_test(bp, cache, mid2, vp);
    EXPECT_EQ(r.type, HitType::Wire);
    EXPECT_EQ(r.wire_index, 1u);  // w2 is the second wire
}

// ============================================================================
// [g1h2i3j4] Regression: hit_test_ports returns wire_id for Bus alias ports
// ============================================================================

TEST(HitTest, PortHit_BusAlias_ReturnsCorrectWireId) {
    Blueprint bp;

    // Bus node with two wires connected
    Node bus; bus.id = "bus1"; bus.name = "bus"; bus.kind = NodeKind::Bus;
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
    for (auto& n : bp.nodes)
        cache.getOrCreate(n, bp.wires);

    // Get position of w2's alias port on the bus
    Pt w2_port_pos = editor_math::get_port_position(bp.nodes[0], "v", bp.wires, "w2", &cache);

    HitResult r = hit_test_ports(bp, cache, w2_port_pos);
    EXPECT_EQ(r.type, HitType::Port);
    EXPECT_EQ(r.port_node_id, "bus1");
    EXPECT_EQ(r.port_name, "v");        // logical port name
    EXPECT_EQ(r.port_wire_id, "w2");    // alias wire ID
}

TEST(HitTest, PortHit_BusMainV_EmptyWireId) {
    Blueprint bp;

    Node bus; bus.id = "bus1"; bus.name = "bus"; bus.kind = NodeKind::Bus;
    bus.at(200, 100).size_wh(80, 32);
    bus.input("v"); bus.output("v");
    bp.add_node(std::move(bus));

    VisualNodeCache cache;
    cache.getOrCreate(bp.nodes[0], bp.wires);

    // Main "v" port (no alias) — wire_id should be empty
    auto* visual = cache.get("bus1");
    ASSERT_NE(visual, nullptr);
    // "v" is the only port (no wires), at index 0
    Pt v_pos = visual->getPort("v")->worldPosition();

    HitResult r = hit_test_ports(bp, cache, v_pos);
    EXPECT_EQ(r.type, HitType::Port);
    EXPECT_EQ(r.port_name, "v");
    EXPECT_TRUE(r.port_wire_id.empty());  // main "v" port, not an alias
}

// =============================================================================
// Visibility Tests: Blueprint Collapsing
// =============================================================================

TEST(HitTestVisibility, HiddenNode_NotHittable) {
    Blueprint bp;

    Node n;
    n.id = "hidden1";
    n.at(100.0f, 50.0f).size_wh(120.0f, 80.0f);
    n.visible = false;  // Hidden (internal blueprint node)
    bp.add_node(std::move(n));

    Viewport vp;
    // Click inside the hidden node's bounds
    auto hit = hit_test(bp, Pt(150.0f, 80.0f), vp);
    EXPECT_EQ(hit.type, HitType::None) << "Hidden node should not be hittable";
}

TEST(HitTestVisibility, HiddenNode_NotHittable_WithCache) {
    Blueprint bp;

    Node n;
    n.id = "hidden1";
    n.at(100.0f, 50.0f).size_wh(120.0f, 80.0f);
    n.visible = false;
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    Viewport vp;

    auto hit = hit_test(bp, cache, Pt(150.0f, 80.0f), vp);
    EXPECT_EQ(hit.type, HitType::None) << "Hidden node should not be hittable (cache overload)";
}

TEST(HitTestVisibility, VisibleNode_StillHittable) {
    Blueprint bp;

    Node n;
    n.id = "vis1";
    n.at(100.0f, 50.0f).size_wh(120.0f, 80.0f);
    n.visible = true;
    bp.add_node(std::move(n));

    Viewport vp;
    auto hit = hit_test(bp, Pt(150.0f, 80.0f), vp);
    EXPECT_EQ(hit.type, HitType::Node) << "Visible node should be hittable";
    EXPECT_EQ(hit.node_index, 0u);
}

TEST(HitTestVisibility, HiddenPort_NotHittable) {
    Blueprint bp;

    Node n;
    n.id = "hidden1";
    n.at(100.0f, 50.0f).size_wh(120.0f, 80.0f);
    n.visible = false;
    n.input("v_in").output("v_out");
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    auto* visual = cache.getOrCreate(bp.nodes[0], bp.wires);
    ASSERT_NE(visual, nullptr);

    // Get port position even though node is hidden
    Pt port_pos = visual->getPort("v_in")->worldPosition();

    auto hit = hit_test_ports(bp, cache, port_pos);
    EXPECT_EQ(hit.type, HitType::None) << "Port on hidden node should not be hittable";
}

TEST(HitTestVisibility, WireToHiddenNode_NotHittable) {
    Blueprint bp;

    Node n1;
    n1.id = "n1";
    n1.at(0.0f, 0.0f).size_wh(100.0f, 50.0f);
    n1.visible = true;
    n1.output("out");
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "n2";
    n2.at(300.0f, 0.0f).size_wh(100.0f, 50.0f);
    n2.visible = false;
    n2.input("in");
    bp.add_node(std::move(n2));

    Wire w;
    w.id = "w1";
    w.start.node_id = "n1";
    w.start.port_name = "out";
    w.end.node_id = "n2";
    w.end.port_name = "in";
    bp.add_wire(std::move(w));

    Viewport vp;
    // Click on the midpoint of where the wire would be
    auto hit = hit_test(bp, Pt(200.0f, 25.0f), vp);
    EXPECT_EQ(hit.type, HitType::None) << "Wire to hidden node should not be hittable";
}

TEST(HitTestVisibility, DrillInOut_CacheSyncsVisibility) {
    Blueprint bp;

    // Collapsed node - visible initially
    Node collapsed;
    collapsed.id = "lamp1";
    collapsed.at(100.0f, 50.0f).size_wh(120.0f, 80.0f);
    collapsed.visible = true;
    collapsed.kind = NodeKind::Blueprint;
    bp.add_node(std::move(collapsed));

    // Internal node - hidden initially
    Node internal;
    internal.id = "lamp1:lamp";
    internal.at(100.0f, 50.0f).size_wh(120.0f, 80.0f);
    internal.visible = false;
    bp.add_node(std::move(internal));

    VisualNodeCache cache;
    Viewport vp;

    // Before drill-in: collapsed is hittable
    auto hit = hit_test(bp, cache, Pt(150.0f, 80.0f), vp);
    EXPECT_EQ(hit.type, HitType::Node);

    // Simulate drill_into: toggle visibility
    bp.nodes[0].visible = false;
    bp.nodes[1].visible = true;

    // After drill-in: collapsed is NOT hittable, internal IS
    hit = hit_test(bp, cache, Pt(150.0f, 80.0f), vp);
    EXPECT_EQ(hit.type, HitType::Node);
    EXPECT_EQ(hit.node_index, 1u) << "After drill-in, internal node should be hittable";
}

