#include <gtest/gtest.h>
#include "editor/hittest.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/viewport.h"
#include "editor/visual_node.h"

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
    Pt port_pos = visual->getPortPosition("v_in");

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
    Pt wire_port_pos = visual->getPortPosition("wire_1");

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
    Pt pos_before = visual.getPortPosition("v");

    // Simulate what an24_editor.cpp does: setSize with node.size
    visual.setSize(Pt(40.0f, 40.0f));

    // Port position must not change
    Pt pos_after = visual.getPortPosition("v");
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
    Pt cached_pos = cached->getPortPosition("v");
    Pt fresh_pos = fresh->getPortPosition("v");
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
    Pt port_pos = visual->getPortPosition("v");

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
    EXPECT_EQ(port->name, "v");  // Was "ref" before fix
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
    Pt port_pos = visual->getPortPosition("v");

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
    Pt v_pos = visual.getPortPosition("v");
    Pt wire_pos = visual.getPortPosition("v", "wire_1");

    // They must be at different positions (off-by-one bug made them the same)
    EXPECT_NE(v_pos.x, wire_pos.x);
}

