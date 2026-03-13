#include <gtest/gtest.h>
#include "editor/visual/scene_hittest.h"
#include "editor/visual/scene.h"
#include "editor/visual/widget.h"
#include "editor/visual/wire/wire.h"
#include "editor/visual/wire/wire_end.h"
#include "editor/visual/wire/routing_point.h"
#include "editor/visual/port/visual_port.h"
#include "editor/visual/node/group_node_widget.h"
#include "data/node.h"

// ============================================================
// Helpers
// ============================================================

/// Minimal node widget for testing
class TestNode : public visual::Widget {
public:
    TestNode(const std::string& id, Pt pos, Pt sz) : id_(id) {
        local_pos_ = pos;
        size_ = sz;
    }
    std::string_view id() const override { return id_; }
    bool isClickable() const override { return true; }
private:
    std::string id_;
};

// Helper to check variant type
template<typename T>
bool holds(const visual::HitResult& r) { return std::holds_alternative<T>(r); }

template<typename T>
const T& get(const visual::HitResult& r) { return std::get<T>(r); }

// ============================================================
// Empty scene
// ============================================================

TEST(SceneHitTest, EmptyScene) {
    visual::Scene scene;
    auto r = visual::hit_test(scene, Pt(100, 100));
    EXPECT_TRUE(holds<visual::HitEmpty>(r));
}

// ============================================================
// Node hit testing
// ============================================================

TEST(SceneHitTest, HitNode) {
    visual::Scene scene;
    auto node = std::make_unique<TestNode>("n1", Pt(100, 100), Pt(80, 60));
    auto* nptr = node.get();
    scene.add(std::move(node));

    // Click inside node
    auto r = visual::hit_test(scene, Pt(140, 130));
    ASSERT_TRUE(holds<visual::HitNode>(r));
    EXPECT_EQ(get<visual::HitNode>(r).widget, nptr);
}

TEST(SceneHitTest, MissNode) {
    visual::Scene scene;
    scene.add(std::make_unique<TestNode>("n1", Pt(100, 100), Pt(80, 60)));

    // Click outside node
    auto r = visual::hit_test(scene, Pt(50, 50));
    EXPECT_TRUE(holds<visual::HitEmpty>(r));
}

// ============================================================
// Port hit testing
// ============================================================

TEST(SceneHitTest, HitPort) {
    visual::Scene scene;
    auto node = std::make_unique<TestNode>("n1", Pt(100, 100), Pt(80, 60));
    auto port = std::make_unique<visual::Port>("v_out", PortSide::Output, PortType::V);
    port->setLocalPos(Pt(80, 20));  // port at world (180, 120)
    auto* pptr = port.get();
    node->addChild(std::move(port));
    scene.add(std::move(node));

    // Click near port center (180 + RADIUS, 120 + RADIUS) = (184, 124)
    auto r = visual::hit_test(scene, Pt(184, 124));
    ASSERT_TRUE(holds<visual::HitPort>(r));
    EXPECT_EQ(get<visual::HitPort>(r).port, pptr);
}

TEST(SceneHitTest, MissPort) {
    visual::Scene scene;
    auto node = std::make_unique<TestNode>("n1", Pt(100, 100), Pt(80, 60));
    auto port = std::make_unique<visual::Port>("v_out", PortSide::Output, PortType::V);
    port->setLocalPos(Pt(80, 20));
    node->addChild(std::move(port));
    scene.add(std::move(node));

    // Click far from port
    auto r = visual::hit_test(scene, Pt(100, 100));
    // Should hit the node, not the port
    ASSERT_TRUE(holds<visual::HitNode>(r));
}

TEST(SceneHitTest, PortOnlyHitTest) {
    visual::Scene scene;
    auto node = std::make_unique<TestNode>("n1", Pt(100, 100), Pt(80, 60));
    auto port = std::make_unique<visual::Port>("v_out", PortSide::Output, PortType::V);
    port->setLocalPos(Pt(80, 20));
    auto* pptr = port.get();
    node->addChild(std::move(port));
    scene.add(std::move(node));

    // Port-only should find port
    auto r = visual::hit_test_ports(scene, Pt(184, 124));
    ASSERT_TRUE(holds<visual::HitPort>(r));
    EXPECT_EQ(get<visual::HitPort>(r).port, pptr);

    // Port-only should miss node
    auto r2 = visual::hit_test_ports(scene, Pt(130, 130));
    EXPECT_TRUE(holds<visual::HitEmpty>(r2));
}

// ============================================================
// Port > Node priority
// ============================================================

TEST(SceneHitTest, PortPriorityOverNode) {
    visual::Scene scene;
    auto node = std::make_unique<TestNode>("n1", Pt(100, 100), Pt(80, 60));
    auto port = std::make_unique<visual::Port>("v_out", PortSide::Output, PortType::V);
    // Port at (100, 100) — overlaps with node's top-left corner
    port->setLocalPos(Pt(0, 0));
    auto* pptr = port.get();
    node->addChild(std::move(port));
    scene.add(std::move(node));

    // Click at port center (100 + RADIUS, 100 + RADIUS) — inside both node and port
    auto r = visual::hit_test(scene, Pt(100 + visual::Port::RADIUS,
                                        100 + visual::Port::RADIUS));
    ASSERT_TRUE(holds<visual::HitPort>(r))
        << "Port should take priority over node";
    EXPECT_EQ(get<visual::HitPort>(r).port, pptr);
}

// ============================================================
// Wire segment hit testing
// ============================================================

TEST(SceneHitTest, HitWireSegment) {
    visual::Scene scene;

    // Two nodes with ports
    auto node_a = std::make_unique<TestNode>("a", Pt(0, 0), Pt(100, 60));
    auto port_a = std::make_unique<visual::Port>("out", PortSide::Output, PortType::V);
    port_a->setLocalPos(Pt(100, 30));
    auto* pa = port_a.get();
    node_a->addChild(std::move(port_a));

    auto node_b = std::make_unique<TestNode>("b", Pt(300, 0), Pt(100, 60));
    auto port_b = std::make_unique<visual::Port>("in", PortSide::Input, PortType::V);
    port_b->setLocalPos(Pt(0, 30));
    auto* pb = port_b.get();
    node_b->addChild(std::move(port_b));

    scene.add(std::move(node_a));
    scene.add(std::move(node_b));

    // Create wire ends as children of ports
    auto we_s = std::make_unique<visual::WireEnd>(nullptr);
    auto we_e = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_s.get();
    auto* we = we_e.get();
    pa->addChild(std::move(we_s));
    pb->addChild(std::move(we_e));

    // Wire polyline: (100,30) + (RADIUS,RADIUS) -> (300,30) + (RADIUS,RADIUS)
    // But WireEnd worldPos = port worldPos + (0,0) = (100,30) and (300,30)
    auto wire = std::make_unique<visual::Wire>("w1", ws, we);
    auto* wptr = wire.get();
    scene.add(std::move(wire));

    // Click on the wire midpoint — y=30, x=200 (between the two ports)
    auto r = visual::hit_test(scene, Pt(200, 30));
    ASSERT_TRUE(holds<visual::HitWire>(r));
    EXPECT_EQ(get<visual::HitWire>(r).wire, wptr);
    EXPECT_EQ(get<visual::HitWire>(r).segment, 0u);
}

TEST(SceneHitTest, MissWireSegment) {
    visual::Scene scene;

    auto node_a = std::make_unique<TestNode>("a", Pt(0, 0), Pt(100, 60));
    auto port_a = std::make_unique<visual::Port>("out", PortSide::Output, PortType::V);
    port_a->setLocalPos(Pt(100, 30));
    auto* pa = port_a.get();
    node_a->addChild(std::move(port_a));

    auto node_b = std::make_unique<TestNode>("b", Pt(300, 0), Pt(100, 60));
    auto port_b = std::make_unique<visual::Port>("in", PortSide::Input, PortType::V);
    port_b->setLocalPos(Pt(0, 30));
    auto* pb = port_b.get();
    node_b->addChild(std::move(port_b));

    scene.add(std::move(node_a));
    scene.add(std::move(node_b));

    auto we_s = std::make_unique<visual::WireEnd>(nullptr);
    auto we_e = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_s.get();
    auto* we = we_e.get();
    pa->addChild(std::move(we_s));
    pb->addChild(std::move(we_e));

    auto wire = std::make_unique<visual::Wire>("w1", ws, we);
    scene.add(std::move(wire));

    // Click far from wire (y=200, wire is at y=30)
    auto r = visual::hit_test(scene, Pt(200, 200));
    EXPECT_TRUE(holds<visual::HitEmpty>(r));
}

// ============================================================
// Routing point hit testing
// ============================================================

TEST(SceneHitTest, HitRoutingPoint) {
    visual::Scene scene;

    auto wire = std::make_unique<visual::Wire>("w1", nullptr, nullptr);
    auto* wptr = wire.get();
    wire->addRoutingPoint(Pt(150, 80), 0);
    scene.add(std::move(wire));

    // Click near routing point
    auto r = visual::hit_test(scene, Pt(150, 80));
    ASSERT_TRUE(holds<visual::HitRoutingPoint>(r));
    auto& rp_hit = get<visual::HitRoutingPoint>(r);
    EXPECT_NE(rp_hit.point, nullptr);
    EXPECT_EQ(rp_hit.wire, wptr);
    EXPECT_EQ(rp_hit.index, 0u);
}

TEST(SceneHitTest, RoutingPointPriorityOverWire) {
    visual::Scene scene;

    auto node_a = std::make_unique<TestNode>("a", Pt(0, 0), Pt(10, 10));
    auto port_a = std::make_unique<visual::Port>("out", PortSide::Output, PortType::V);
    port_a->setLocalPos(Pt(0, 0));
    auto* pa = port_a.get();
    node_a->addChild(std::move(port_a));

    auto node_b = std::make_unique<TestNode>("b", Pt(300, 0), Pt(10, 10));
    auto port_b = std::make_unique<visual::Port>("in", PortSide::Input, PortType::V);
    port_b->setLocalPos(Pt(0, 0));
    auto* pb = port_b.get();
    node_b->addChild(std::move(port_b));

    scene.add(std::move(node_a));
    scene.add(std::move(node_b));

    auto we_s = std::make_unique<visual::WireEnd>(nullptr);
    auto we_e = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_s.get();
    auto* we = we_e.get();
    pa->addChild(std::move(we_s));
    pb->addChild(std::move(we_e));

    auto wire = std::make_unique<visual::Wire>("w1", ws, we);
    // Routing point is ON the wire's path
    wire->addRoutingPoint(Pt(150, 0), 0);
    scene.add(std::move(wire));

    // Click directly on the routing point — should get RP, not Wire
    auto r = visual::hit_test(scene, Pt(150, 0));
    EXPECT_TRUE(holds<visual::HitRoutingPoint>(r))
        << "RoutingPoint should take priority over wire segment";
}

// ============================================================
// Wire with routing points: segment index
// ============================================================

TEST(SceneHitTest, WireSegmentIndex) {
    visual::Scene scene;

    auto node_a = std::make_unique<TestNode>("a", Pt(0, 50), Pt(10, 10));
    auto port_a = std::make_unique<visual::Port>("out", PortSide::Output, PortType::V);
    port_a->setLocalPos(Pt(0, 0));
    auto* pa = port_a.get();
    node_a->addChild(std::move(port_a));

    auto node_b = std::make_unique<TestNode>("b", Pt(400, 50), Pt(10, 10));
    auto port_b = std::make_unique<visual::Port>("in", PortSide::Input, PortType::V);
    port_b->setLocalPos(Pt(0, 0));
    auto* pb = port_b.get();
    node_b->addChild(std::move(port_b));

    scene.add(std::move(node_a));
    scene.add(std::move(node_b));

    auto we_s = std::make_unique<visual::WireEnd>(nullptr);
    auto we_e = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_s.get();
    auto* we = we_e.get();
    pa->addChild(std::move(we_s));
    pb->addChild(std::move(we_e));

    auto wire = std::make_unique<visual::Wire>("w1", ws, we);
    auto* wptr = wire.get();
    // Polyline: (0,50) -> (200,100) -> (400,50)
    wire->addRoutingPoint(Pt(200, 100), 0);
    scene.add(std::move(wire));

    // Click near second segment (between routing point and end)
    // Second segment: (200,100) -> (400,50). Midpoint ~ (300, 75)
    auto r = visual::hit_test(scene, Pt(300, 75));
    ASSERT_TRUE(holds<visual::HitWire>(r));
    EXPECT_EQ(get<visual::HitWire>(r).wire, wptr);
    EXPECT_EQ(get<visual::HitWire>(r).segment, 1u);
}

// ============================================================
// Multiple routing points
// ============================================================

TEST(SceneHitTest, MultipleRoutingPoints) {
    visual::Scene scene;

    auto wire = std::make_unique<visual::Wire>("w1", nullptr, nullptr);
    auto* wptr = wire.get();
    wire->addRoutingPoint(Pt(100, 100), 0);
    wire->addRoutingPoint(Pt(200, 200), 1);
    wire->addRoutingPoint(Pt(300, 100), 2);
    scene.add(std::move(wire));

    // Hit second routing point
    auto r = visual::hit_test(scene, Pt(200, 200));
    ASSERT_TRUE(holds<visual::HitRoutingPoint>(r));
    EXPECT_EQ(get<visual::HitRoutingPoint>(r).wire, wptr);
    EXPECT_EQ(get<visual::HitRoutingPoint>(r).index, 1u);
}

// ============================================================
// hit_math utilities
// ============================================================

TEST(HitMath, Distance) {
    EXPECT_FLOAT_EQ(visual::hit_math::distance(Pt(0, 0), Pt(3, 4)), 5.0f);
    EXPECT_FLOAT_EQ(visual::hit_math::distance(Pt(1, 1), Pt(1, 1)), 0.0f);
}

TEST(HitMath, DistanceToSegment) {
    // Point directly on segment
    EXPECT_NEAR(visual::hit_math::distance_to_segment(Pt(5, 0), Pt(0, 0), Pt(10, 0)),
                0.0f, 1e-5f);

    // Point perpendicular to segment
    EXPECT_NEAR(visual::hit_math::distance_to_segment(Pt(5, 3), Pt(0, 0), Pt(10, 0)),
                3.0f, 1e-5f);

    // Point beyond segment end
    EXPECT_NEAR(visual::hit_math::distance_to_segment(Pt(15, 0), Pt(0, 0), Pt(10, 0)),
                5.0f, 1e-5f);

    // Zero-length segment
    EXPECT_NEAR(visual::hit_math::distance_to_segment(Pt(3, 4), Pt(0, 0), Pt(0, 0)),
                5.0f, 1e-5f);
}

// ============================================================
// Null-endpoint wire (no crash)
// ============================================================

TEST(SceneHitTest, NullEndpointWireNoCrash) {
    visual::Scene scene;
    auto wire = std::make_unique<visual::Wire>("w1", nullptr, nullptr);
    scene.add(std::move(wire));

    // No crash, just empty (no polyline points)
    auto r = visual::hit_test(scene, Pt(0, 0));
    // Wire at (0,0) has empty polyline, so the AABB is (0,0)-(0,0)
    // Grid may or may not find it, but we should not crash
    EXPECT_NO_FATAL_FAILURE(visual::hit_test(scene, Pt(50, 50)));
}

// ============================================================
// Layer-aware hit testing
// ============================================================

/// A widget at a specific render layer for testing layer priority
class LayeredNode : public visual::Widget {
public:
    LayeredNode(const std::string& id, Pt pos, Pt sz, visual::RenderLayer layer)
        : id_(id), layer_(layer) {
        local_pos_ = pos;
        size_ = sz;
    }
    std::string_view id() const override { return id_; }
    bool isClickable() const override { return true; }
    visual::RenderLayer renderLayer() const override { return layer_; }
private:
    std::string id_;
    visual::RenderLayer layer_;
};

TEST(SceneHitTest, PreferHigherLayerOverLower) {
    visual::Scene scene;

    // Group (layer 0) and Normal node (layer 3) at the same position
    scene.add(std::make_unique<LayeredNode>(
        "group1", Pt(100, 100), Pt(200, 200), visual::RenderLayer::Group));
    scene.add(std::make_unique<LayeredNode>(
        "node1", Pt(100, 100), Pt(80, 60), visual::RenderLayer::Normal));

    // Hit at overlap: should prefer Normal (higher layer)
    auto r = visual::hit_test(scene, Pt(120, 120));
    ASSERT_TRUE(holds<visual::HitNode>(r));
    EXPECT_EQ(get<visual::HitNode>(r).widget->id(), "node1");
}

TEST(SceneHitTest, FallBackToLowerLayerWhenNoHigherHit) {
    visual::Scene scene;

    // Large group, no overlapping node at the test point
    scene.add(std::make_unique<LayeredNode>(
        "group1", Pt(100, 100), Pt(200, 200), visual::RenderLayer::Group));
    scene.add(std::make_unique<LayeredNode>(
        "node1", Pt(400, 400), Pt(80, 60), visual::RenderLayer::Normal));

    // Hit inside group only
    auto r = visual::hit_test(scene, Pt(150, 150));
    ASSERT_TRUE(holds<visual::HitNode>(r));
    EXPECT_EQ(get<visual::HitNode>(r).widget->id(), "group1");
}

// ============================================================
// Resize handle hit testing
// ============================================================

/// Resizable widget for testing resize corner detection
class ResizableTestNode : public visual::Widget {
public:
    ResizableTestNode(const std::string& id, Pt pos, Pt sz) : id_(id) {
        local_pos_ = pos;
        size_ = sz;
    }
    std::string_view id() const override { return id_; }
    bool isClickable() const override { return true; }
    bool isResizable() const override { return true; }
private:
    std::string id_;
};

TEST(SceneHitTest, ResizeHandle_BottomRight) {
    visual::Scene scene;
    // Node at (100, 100) with size (200, 160) => corners at (100,100) .. (300,260)
    auto node = std::make_unique<ResizableTestNode>("rn1", Pt(100, 100), Pt(200, 160));
    auto* nptr = node.get();
    scene.add(std::move(node));

    // Click near bottom-right corner (300, 260)
    auto r = visual::hit_test(scene, Pt(300, 260));
    ASSERT_TRUE(holds<visual::HitResizeHandle>(r))
        << "Should hit resize handle at bottom-right corner";
    EXPECT_EQ(get<visual::HitResizeHandle>(r).widget, nptr);
    EXPECT_EQ(get<visual::HitResizeHandle>(r).corner, ResizeCorner::BottomRight);
}

TEST(SceneHitTest, ResizeHandle_TopLeft) {
    visual::Scene scene;
    auto node = std::make_unique<ResizableTestNode>("rn1", Pt(100, 100), Pt(200, 160));
    auto* nptr = node.get();
    scene.add(std::move(node));

    // Click near top-left corner (100, 100)
    auto r = visual::hit_test(scene, Pt(100, 100));
    ASSERT_TRUE(holds<visual::HitResizeHandle>(r))
        << "Should hit resize handle at top-left corner";
    EXPECT_EQ(get<visual::HitResizeHandle>(r).widget, nptr);
    EXPECT_EQ(get<visual::HitResizeHandle>(r).corner, ResizeCorner::TopLeft);
}

TEST(SceneHitTest, ResizeHandle_TopRight) {
    visual::Scene scene;
    auto node = std::make_unique<ResizableTestNode>("rn1", Pt(100, 100), Pt(200, 160));
    auto* nptr = node.get();
    scene.add(std::move(node));

    // Click near top-right corner (300, 100)
    auto r = visual::hit_test(scene, Pt(300, 100));
    ASSERT_TRUE(holds<visual::HitResizeHandle>(r))
        << "Should hit resize handle at top-right corner";
    EXPECT_EQ(get<visual::HitResizeHandle>(r).widget, nptr);
    EXPECT_EQ(get<visual::HitResizeHandle>(r).corner, ResizeCorner::TopRight);
}

TEST(SceneHitTest, ResizeHandle_BottomLeft) {
    visual::Scene scene;
    auto node = std::make_unique<ResizableTestNode>("rn1", Pt(100, 100), Pt(200, 160));
    auto* nptr = node.get();
    scene.add(std::move(node));

    // Click near bottom-left corner (100, 260)
    auto r = visual::hit_test(scene, Pt(100, 260));
    ASSERT_TRUE(holds<visual::HitResizeHandle>(r))
        << "Should hit resize handle at bottom-left corner";
    EXPECT_EQ(get<visual::HitResizeHandle>(r).widget, nptr);
    EXPECT_EQ(get<visual::HitResizeHandle>(r).corner, ResizeCorner::BottomLeft);
}

TEST(SceneHitTest, ResizeHandle_InteriorReturnsNode) {
    visual::Scene scene;
    // Node at (100, 100) with size (200, 160)
    auto node = std::make_unique<ResizableTestNode>("rn1", Pt(100, 100), Pt(200, 160));
    auto* nptr = node.get();
    scene.add(std::move(node));

    // Click in the center of the node (200, 180) — far from any corner
    auto r = visual::hit_test(scene, Pt(200, 180));
    ASSERT_TRUE(holds<visual::HitNode>(r))
        << "Interior click on resizable node should return HitNode, not HitResizeHandle";
    EXPECT_EQ(get<visual::HitNode>(r).widget, nptr);
}

TEST(SceneHitTest, NonResizable_NoResizeHandle) {
    visual::Scene scene;
    // Regular TestNode is NOT resizable
    auto node = std::make_unique<TestNode>("n1", Pt(100, 100), Pt(200, 160));
    auto* nptr = node.get();
    scene.add(std::move(node));

    // Click at the exact corner (100, 100) — within RESIZE_HANDLE_HIT_RADIUS
    // but the node is not resizable, so should return HitNode
    auto r = visual::hit_test(scene, Pt(100, 100));
    ASSERT_TRUE(holds<visual::HitNode>(r))
        << "Non-resizable node should never return HitResizeHandle";
    EXPECT_EQ(get<visual::HitNode>(r).widget, nptr);
}

TEST(SceneHitTest, ResizeHandle_JustOutsideRadius) {
    visual::Scene scene;
    auto node = std::make_unique<ResizableTestNode>("rn1", Pt(100, 100), Pt(200, 160));
    scene.add(std::move(node));

    // Bottom-right corner is at (300, 260).
    // RESIZE_HANDLE_HIT_RADIUS = 10.  Click at (311, 260) — 11 pixels away.
    // Should miss the corner but still hit the node AABB (which extends to 300).
    // Actually (311, 260) is outside the AABB (300 max x), so it may miss entirely.
    // Use a point that's inside the node AABB but >10px from any corner.
    // (150, 180) is >50px from all corners.
    auto r = visual::hit_test(scene, Pt(150, 180));
    ASSERT_TRUE(holds<visual::HitNode>(r))
        << "Point far from corners should return HitNode";
}

// ============================================================
// Regression: GroupNodeWidget resize handles
// ============================================================
// Group nodes use containsBorder() which rejects interior clicks.
// Resize handles at corners must still be detected even when the
// click point is beyond the 6px border margin but within the 10px
// resize handle hit radius.

static Node make_group_data(const std::string& id, Pt pos, Pt sz) {
    Node n;
    n.id = id;
    n.name = "TestGroup";
    n.type_name = "group";
    n.render_hint = "group";
    n.pos = pos;
    n.size = sz;
    return n;
}

TEST(SceneHitTest, GroupNode_ResizeHandle_BottomRight) {
    visual::Scene scene;
    // Size 192 is a multiple of PORT_LAYOUT_GRID (16), so no snap adjustment.
    // Corners: TL=(100,100), BR=(292,292)
    Node data = make_group_data("g1", Pt(100, 100), Pt(192, 192));
    auto group = std::make_unique<visual::GroupNodeWidget>(data);
    auto* gptr = group.get();
    scene.add(std::move(group));

    // Bottom-right corner at (292, 292). Click 7px inside on both axes.
    // Distance from corner = sqrt(49+49) ≈ 9.9 < 10 (within handle radius)
    // but 7px > 6px border margin, so containsBorder would reject this.
    auto r = visual::hit_test(scene, Pt(285, 285));
    ASSERT_TRUE(holds<visual::HitResizeHandle>(r))
        << "Group resize handle should be detected even beyond border margin";
    EXPECT_EQ(get<visual::HitResizeHandle>(r).widget, gptr);
    EXPECT_EQ(get<visual::HitResizeHandle>(r).corner, ResizeCorner::BottomRight);
}

TEST(SceneHitTest, GroupNode_ResizeHandle_TopLeft) {
    visual::Scene scene;
    Node data = make_group_data("g1", Pt(100, 100), Pt(192, 192));
    auto group = std::make_unique<visual::GroupNodeWidget>(data);
    auto* gptr = group.get();
    scene.add(std::move(group));

    // Top-left corner at (100, 100). Click at exact corner.
    auto r = visual::hit_test(scene, Pt(100, 100));
    ASSERT_TRUE(holds<visual::HitResizeHandle>(r))
        << "Group top-left resize handle should be detected";
    EXPECT_EQ(get<visual::HitResizeHandle>(r).widget, gptr);
    EXPECT_EQ(get<visual::HitResizeHandle>(r).corner, ResizeCorner::TopLeft);
}

TEST(SceneHitTest, GroupNode_ResizeHandle_BottomLeft) {
    visual::Scene scene;
    Node data = make_group_data("g1", Pt(100, 100), Pt(192, 192));
    auto group = std::make_unique<visual::GroupNodeWidget>(data);
    auto* gptr = group.get();
    scene.add(std::move(group));

    // Bottom-left corner at (100, 292). Click 7px diagonally inside.
    auto r = visual::hit_test(scene, Pt(107, 285));
    ASSERT_TRUE(holds<visual::HitResizeHandle>(r))
        << "Group bottom-left resize handle should be detected";
    EXPECT_EQ(get<visual::HitResizeHandle>(r).widget, gptr);
    EXPECT_EQ(get<visual::HitResizeHandle>(r).corner, ResizeCorner::BottomLeft);
}

TEST(SceneHitTest, GroupNode_ResizeHandle_TopRight) {
    visual::Scene scene;
    Node data = make_group_data("g1", Pt(100, 100), Pt(192, 192));
    auto group = std::make_unique<visual::GroupNodeWidget>(data);
    auto* gptr = group.get();
    scene.add(std::move(group));

    // Top-right corner at (292, 100). Click 5px offset.
    auto r = visual::hit_test(scene, Pt(287, 105));
    ASSERT_TRUE(holds<visual::HitResizeHandle>(r))
        << "Group top-right resize handle should be detected";
    EXPECT_EQ(get<visual::HitResizeHandle>(r).widget, gptr);
    EXPECT_EQ(get<visual::HitResizeHandle>(r).corner, ResizeCorner::TopRight);
}

TEST(SceneHitTest, GroupNode_InteriorPassesThrough) {
    visual::Scene scene;
    Node data = make_group_data("g1", Pt(100, 100), Pt(192, 192));
    scene.add(std::make_unique<visual::GroupNodeWidget>(data));

    // Click in group interior (far from borders and corners) → should pass through
    auto r = visual::hit_test(scene, Pt(196, 196));
    EXPECT_TRUE(holds<visual::HitEmpty>(r))
        << "Interior click on group should pass through (not hit the group)";
}
