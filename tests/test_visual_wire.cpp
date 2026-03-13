#include "ui/math/pt.h"

using ui::Pt;

#include <gtest/gtest.h>
#include "editor/visual/widget.h"
#include "ui/core/grid.h"
#include "editor/visual/scene.h"
#include "editor/visual/wire/wire.h"
#include "editor/visual/wire/wire_end.h"
#include "editor/visual/wire/routing_point.h"
#include "editor/visual/port/visual_port.h"

// ============================================================
// Helpers
// ============================================================

/// Minimal parent to host a Port (simulates a NodeWidget)
class FakeNode : public visual::Widget {
public:
    FakeNode(const std::string& id, Pt pos) : id_(id) {
        local_pos_ = pos;
        size_ = Pt(100, 60);
    }
    std::string_view id() const override { return id_; }

private:
    std::string id_;
};

// ============================================================
// Construction & Properties
// ============================================================

TEST(WireTest, Construction) {
    visual::WireEnd start(nullptr);
    visual::WireEnd end(nullptr);
    visual::Wire wire("w1", &start, &end);

    EXPECT_EQ(wire.start(), &start);
    EXPECT_EQ(wire.end(), &end);
}

TEST(WireTest, Id) {
    visual::Wire wire("wire_42", nullptr, nullptr);
    EXPECT_EQ(wire.id(), "wire_42");
}

TEST(WireTest, WireIsClickable) {
    visual::Wire wire("w", nullptr, nullptr);
    EXPECT_TRUE(wire.isClickable());
}

TEST(WireTest, WireEndNotClickable) {
    visual::WireEnd we(nullptr);
    EXPECT_FALSE(we.isClickable());
}

TEST(WireTest, RoutingPointIsClickable) {
    visual::RoutingPoint rp(Pt(10, 20));
    EXPECT_TRUE(rp.isClickable());
}

// ============================================================
// Polyline
// ============================================================

TEST(WireTest, PolylineBasic) {
    FakeNode node_a("a", Pt(0, 0));
    FakeNode node_b("b", Pt(200, 0));

    auto port_a = std::make_unique<visual::Port>("out", PortSide::Output, PortType::V);
    port_a->setLocalPos(Pt(100, 30));
    auto* pa = port_a.get();
    node_a.addChild(std::move(port_a));

    auto port_b = std::make_unique<visual::Port>("in", PortSide::Input, PortType::V);
    port_b->setLocalPos(Pt(0, 30));
    auto* pb = port_b.get();
    node_b.addChild(std::move(port_b));

    auto we_start = std::make_unique<visual::WireEnd>(nullptr);
    auto we_end = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_start.get();
    auto* we = we_end.get();
    pa->addChild(std::move(we_start));
    pb->addChild(std::move(we_end));

    visual::Wire wire("w1", ws, we);

    auto pl = wire.polyline();
    ASSERT_EQ(pl.size(), 2u);
    // start world = node_a(0,0) + port(100,30) + wireEnd(RADIUS,RADIUS)
    EXPECT_FLOAT_EQ(pl[0].x, 104.0f);
    EXPECT_FLOAT_EQ(pl[0].y, 34.0f);
    // end world = node_b(200,0) + port(0,30) + wireEnd(RADIUS,RADIUS)
    EXPECT_FLOAT_EQ(pl[1].x, 204.0f);
    EXPECT_FLOAT_EQ(pl[1].y, 34.0f);
}

TEST(WireTest, PolylineWithRouting) {
    FakeNode node_a("a", Pt(0, 0));
    FakeNode node_b("b", Pt(200, 0));

    auto pa = std::make_unique<visual::Port>("out", PortSide::Output, PortType::V);
    pa->setLocalPos(Pt(100, 30));
    auto* pa_ptr = pa.get();
    node_a.addChild(std::move(pa));

    auto pb = std::make_unique<visual::Port>("in", PortSide::Input, PortType::V);
    pb->setLocalPos(Pt(0, 30));
    auto* pb_ptr = pb.get();
    node_b.addChild(std::move(pb));

    auto we_s = std::make_unique<visual::WireEnd>(nullptr);
    auto we_e = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_s.get();
    auto* we = we_e.get();
    pa_ptr->addChild(std::move(we_s));
    pb_ptr->addChild(std::move(we_e));

    visual::Wire wire("w1", ws, we);
    wire.addRoutingPoint(Pt(150, 50), 0);

    auto pl = wire.polyline();
    ASSERT_EQ(pl.size(), 3u);
    EXPECT_FLOAT_EQ(pl[0].x, 104.0f);   // start (port center)
    EXPECT_FLOAT_EQ(pl[1].x, 150.0f);   // routing point (worldPos = wire(0,0) + rp(150,50))
    EXPECT_FLOAT_EQ(pl[1].y, 50.0f);
    EXPECT_FLOAT_EQ(pl[2].x, 204.0f);   // end (port center)
}

TEST(WireTest, PolylineNullStart) {
    FakeNode node_b("b", Pt(200, 0));
    auto pb = std::make_unique<visual::Port>("in", PortSide::Input, PortType::V);
    pb->setLocalPos(Pt(0, 30));
    auto* pb_ptr = pb.get();
    node_b.addChild(std::move(pb));

    auto we_e = std::make_unique<visual::WireEnd>(nullptr);
    auto* we = we_e.get();
    pb_ptr->addChild(std::move(we_e));

    visual::Wire wire("w1", nullptr, we);
    auto pl = wire.polyline();
    ASSERT_EQ(pl.size(), 1u);
    EXPECT_FLOAT_EQ(pl[0].x, 204.0f);
    EXPECT_FLOAT_EQ(pl[0].y, 34.0f);
}

TEST(WireTest, PolylineNullBoth) {
    visual::Wire wire("w1", nullptr, nullptr);
    auto pl = wire.polyline();
    EXPECT_TRUE(pl.empty());
}

// ============================================================
// Bounding Box (virtual worldMin/worldMax override)
// ============================================================

TEST(WireTest, BoundsFromPolyline) {
    FakeNode node_a("a", Pt(100, 100));
    FakeNode node_b("b", Pt(300, 200));

    auto pa = std::make_unique<visual::Port>("out", PortSide::Output, PortType::V);
    pa->setLocalPos(Pt(0, 0));
    auto* pa_ptr = pa.get();
    node_a.addChild(std::move(pa));

    auto pb = std::make_unique<visual::Port>("in", PortSide::Input, PortType::V);
    pb->setLocalPos(Pt(0, 0));
    auto* pb_ptr = pb.get();
    node_b.addChild(std::move(pb));

    auto we_s = std::make_unique<visual::WireEnd>(nullptr);
    auto we_e = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_s.get();
    auto* we = we_e.get();
    pa_ptr->addChild(std::move(we_s));
    pb_ptr->addChild(std::move(we_e));

    visual::Wire wire("w1", ws, we);

    // Polyline: (104,104) -> (304,204) — port center offsets
    // worldMin = (104-4, 104-4) = (100, 100)
    // worldMax = (304+4, 204+4) = (308, 208)
    Pt mn = wire.worldMin();
    Pt mx = wire.worldMax();
    EXPECT_FLOAT_EQ(mn.x, 100.0f);
    EXPECT_FLOAT_EQ(mn.y, 100.0f);
    EXPECT_FLOAT_EQ(mx.x, 308.0f);
    EXPECT_FLOAT_EQ(mx.y, 208.0f);
}

TEST(WireTest, BoundsEmptyPolyline) {
    visual::Wire wire("w", nullptr, nullptr);
    Pt mn = wire.worldMin();
    Pt mx = wire.worldMax();
    EXPECT_FLOAT_EQ(mn.x, 0.0f);
    EXPECT_FLOAT_EQ(mn.y, 0.0f);
    EXPECT_FLOAT_EQ(mx.x, 0.0f);
    EXPECT_FLOAT_EQ(mx.y, 0.0f);
}

TEST(WireTest, BoundsVirtualDispatch) {
    // Verify Grid sees the overridden worldMin/worldMax via Widget*
    FakeNode node_a("a", Pt(100, 100));
    auto pa = std::make_unique<visual::Port>("out", PortSide::Output, PortType::V);
    pa->setLocalPos(Pt(0, 0));
    auto* pa_ptr = pa.get();
    node_a.addChild(std::move(pa));

    auto we_s = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_s.get();
    pa_ptr->addChild(std::move(we_s));

    auto wire = std::make_unique<visual::Wire>("w1", ws, nullptr);
    visual::Widget* w = wire.get(); // base pointer

    // Via base pointer, should still get Wire's override
    Pt mn = w->worldMin();
    EXPECT_FLOAT_EQ(mn.x, 100.0f);
    EXPECT_FLOAT_EQ(mn.y, 100.0f);
}

// ============================================================
// Routing Points
// ============================================================

TEST(WireTest, AddRoutingPoint) {
    visual::Wire wire("w", nullptr, nullptr);
    auto* rp = wire.addRoutingPoint(Pt(50, 60), 0);
    EXPECT_NE(rp, nullptr);
    EXPECT_EQ(wire.children().size(), 1u);
}

TEST(WireTest, AddRoutingPointOrdered) {
    visual::Wire wire("w", nullptr, nullptr);
    wire.addRoutingPoint(Pt(10, 0), 0);
    wire.addRoutingPoint(Pt(30, 0), 1);
    wire.addRoutingPoint(Pt(20, 0), 1); // insert between

    ASSERT_EQ(wire.children().size(), 3u);
    EXPECT_FLOAT_EQ(wire.children()[0]->localPos().x, 10.0f);
    EXPECT_FLOAT_EQ(wire.children()[1]->localPos().x, 20.0f);
    EXPECT_FLOAT_EQ(wire.children()[2]->localPos().x, 30.0f);
}

TEST(WireTest, RemoveRoutingPoint) {
    visual::Wire wire("w", nullptr, nullptr);
    wire.addRoutingPoint(Pt(50, 60), 0);
    EXPECT_EQ(wire.children().size(), 1u);
    wire.removeRoutingPoint(0);
    EXPECT_EQ(wire.children().size(), 0u);
}

// ============================================================
// Cascade Destruction
// ============================================================

TEST(WireTest, CascadeDestructionViaPort) {
    visual::Scene scene;

    // Create a node with a port
    auto node = std::make_unique<FakeNode>("node1", Pt(0, 0));
    auto port = std::make_unique<visual::Port>("out", PortSide::Output, PortType::V);
    auto* port_ptr = port.get();
    node->addChild(std::move(port));
    scene.add(std::move(node));

    // Create wire (initially with null endpoints — we'll wire it up manually)
    auto wire = std::make_unique<visual::Wire>("w1", nullptr, nullptr);
    auto* wire_ptr = wire.get();

    // Create WireEnd pointing to the wire, add to port
    auto we = std::make_unique<visual::WireEnd>(wire_ptr);
    port_ptr->addChild(std::move(we));

    scene.add(std::move(wire));
    EXPECT_EQ(scene.roots().size(), 2u); // node + wire

    // Remove the node — destroys Port and WireEnd
    scene.remove(scene.find("node1"));
    scene.flushRemovals(); // node destroyed, WireEnd destructor fires onEndpointDestroyed

    // Wire should be pending removal now (cascaded)
    scene.flushRemovals(); // flush the wire removal
    EXPECT_EQ(scene.roots().size(), 0u);
}

TEST(WireTest, WireDestructorClearsWireEnds) {
    auto we_start = std::make_unique<visual::WireEnd>(nullptr);
    auto we_end = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_start.get();
    auto* we = we_end.get();

    {
        visual::Wire wire("w1", ws, we);
        EXPECT_EQ(ws->wire(), &wire);
        EXPECT_EQ(we->wire(), &wire);
    }
    // Wire destroyed — WireEnds should have wire pointers cleared
    EXPECT_EQ(ws->wire(), nullptr);
    EXPECT_EQ(we->wire(), nullptr);
}

TEST(WireTest, ClearWireBreaksBothDirections) {
    auto we_start = std::make_unique<visual::WireEnd>(nullptr);
    auto we_end = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_start.get();
    auto* we = we_end.get();

    visual::Wire wire("w1", ws, we);
    EXPECT_EQ(ws->wire(), &wire);
    EXPECT_EQ(we->wire(), &wire);
    EXPECT_EQ(wire.start(), ws);
    EXPECT_EQ(wire.end(), we);

    // clearWire() should break the connection in BOTH directions
    ws->clearWire();

    EXPECT_EQ(ws->wire(), nullptr);      // WireEnd -> Wire broken
    EXPECT_EQ(wire.start(), nullptr);    // Wire -> WireEnd broken
    EXPECT_EQ(we->wire(), &wire);        // Other end unaffected
    EXPECT_EQ(wire.end(), we);

    // Clear the other end
    we->clearWire();

    EXPECT_EQ(we->wire(), nullptr);      // WireEnd -> Wire broken
    EXPECT_EQ(wire.end(), nullptr);      // Wire -> WireEnd broken
}

TEST(WireTest, ClearWireIsIdempotent) {
    auto we = std::make_unique<visual::WireEnd>(nullptr);
    auto* we_ptr = we.get();

    visual::Wire wire("w1", we_ptr, nullptr);

    // Call clearWire multiple times - should be safe
    we_ptr->clearWire();
    we_ptr->clearWire();
    we_ptr->clearWire();

    EXPECT_EQ(we_ptr->wire(), nullptr);
    EXPECT_EQ(wire.start(), nullptr);
}

// ============================================================
// Scene Integration
// ============================================================

TEST(WireTest, SceneIntegration) {
    visual::Scene scene;

    FakeNode node_a("a", Pt(100, 100));
    auto pa = std::make_unique<visual::Port>("out", PortSide::Output, PortType::V);
    pa->setLocalPos(Pt(0, 0));
    auto* pa_ptr = pa.get();
    node_a.addChild(std::move(pa));

    auto we_s = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_s.get();
    pa_ptr->addChild(std::move(we_s));

    auto wire = std::make_unique<visual::Wire>("w1", ws, nullptr);
    scene.add(std::move(wire));

    EXPECT_NE(scene.find("w1"), nullptr);

    // Wire should be queryable in Grid near its endpoint
    auto results = scene.grid().queryAs<visual::Wire>(Pt(100, 100), 10.0f);
    EXPECT_GE(results.size(), 1u);
}

TEST(WireTest, RenderNoCrash) {
    visual::Wire wire("w", nullptr, nullptr);
    visual::RenderContext ctx;
    ctx.zoom = 1.0f;
    EXPECT_NO_FATAL_FAILURE(wire.render(nullptr, ctx));
}

// ============================================================
// Crossing Detection & Arc/Gap Rendering
// ============================================================

/// Helper: create a Wire with no WireEnds but with routing points
/// that define a polyline. Wire localPos stays at (0,0).
static visual::Wire* makePolylineWire(visual::Scene& scene,
                                       const std::string& id,
                                       const std::vector<Pt>& pts) {
    auto wire = std::make_unique<visual::Wire>(id, nullptr, nullptr);
    auto* w = wire.get();
    // Add routing points to define the polyline
    for (size_t i = 0; i < pts.size(); ++i) {
        w->addRoutingPoint(pts[i], i);
    }
    scene.add(std::move(wire));
    return w;
}

TEST(WireTest, CrossingDetected_BasicCross) {
    visual::Scene scene;

    // Horizontal wire through (0,50) -> (100,50)
    auto* w1 = makePolylineWire(scene, "w1", {{0, 50}, {100, 50}});
    // Vertical wire through (50,0) -> (50,100) — crosses at (50,50)
    auto* w2 = makePolylineWire(scene, "w2", {{50, 0}, {50, 100}});

    visual::compute_wire_crossings(scene);

    // Both wires should have exactly one crossing
    ASSERT_EQ(w1->crossings().size(), 1u);
    ASSERT_EQ(w2->crossings().size(), 1u);

    // Crossing position should be at (50,50)
    EXPECT_NEAR(w1->crossings()[0].pos.x, 50.0f, 0.5f);
    EXPECT_NEAR(w1->crossings()[0].pos.y, 50.0f, 0.5f);
    EXPECT_NEAR(w2->crossings()[0].pos.x, 50.0f, 0.5f);
    EXPECT_NEAR(w2->crossings()[0].pos.y, 50.0f, 0.5f);

    // One should be gap (draw_arc=false), the other arc (draw_arc=true)
    EXPECT_NE(w1->crossings()[0].draw_arc, w2->crossings()[0].draw_arc);
}

TEST(WireTest, CrossingDetected_AtGridCellBoundary) {
    // Grid cell size is 64.0f. Place wires so the crossing is exactly
    // at x=64 (cell boundary between cell 0 and cell 1).
    visual::Scene scene;

    // Horizontal wire: y=64, x from 0 to 128 (spans two grid cells)
    auto* w1 = makePolylineWire(scene, "w1", {{0, 64}, {128, 64}});
    // Vertical wire: x=64, y from 0 to 128 — crosses at (64,64)
    auto* w2 = makePolylineWire(scene, "w2", {{64, 0}, {64, 128}});

    visual::compute_wire_crossings(scene);

    // Both wires must have the crossing detected even at the cell boundary
    ASSERT_EQ(w1->crossings().size(), 1u);
    ASSERT_EQ(w2->crossings().size(), 1u);
    EXPECT_NEAR(w1->crossings()[0].pos.x, 64.0f, 0.5f);
    EXPECT_NEAR(w1->crossings()[0].pos.y, 64.0f, 0.5f);
}

TEST(WireTest, CrossingDetected_ParallelNoCross) {
    visual::Scene scene;

    // Two horizontal wires that never cross
    auto* w1 = makePolylineWire(scene, "w1", {{0, 50}, {100, 50}});
    auto* w2 = makePolylineWire(scene, "w2", {{0, 80}, {100, 80}});

    visual::compute_wire_crossings(scene);

    EXPECT_EQ(w1->crossings().size(), 0u);
    EXPECT_EQ(w2->crossings().size(), 0u);
}

TEST(WireTest, CrossingArcGapAssignment) {
    // Verify that the lower-index wire gets gap (draw_arc=false)
    // and the higher-index wire gets arc (draw_arc=true).
    visual::Scene scene;

    auto* w1 = makePolylineWire(scene, "w1", {{0, 50}, {100, 50}});
    auto* w2 = makePolylineWire(scene, "w2", {{50, 0}, {50, 100}});

    visual::compute_wire_crossings(scene);

    ASSERT_EQ(w1->crossings().size(), 1u);
    ASSERT_EQ(w2->crossings().size(), 1u);

    // w1 is added first (lower index) -> gap only
    EXPECT_FALSE(w1->crossings()[0].draw_arc);
    // w2 is added second (higher index) -> draws arc
    EXPECT_TRUE(w2->crossings()[0].draw_arc);
}

TEST(WireTest, CrossingDetected_MultipleIntersections) {
    // A zigzag wire crossing a straight wire at two points
    visual::Scene scene;

    // Horizontal wire
    auto* w1 = makePolylineWire(scene, "w1", {{0, 50}, {200, 50}});
    // Zigzag that crosses horizontal twice
    auto* w2 = makePolylineWire(scene, "w2", {{30, 0}, {30, 100}, {100, 100}, {100, 0}});

    visual::compute_wire_crossings(scene);

    // w1 should have 2 crossings (at x=30 and x=100 on y=50)
    EXPECT_EQ(w1->crossings().size(), 2u);
    // w2 should have 2 crossings as well
    EXPECT_EQ(w2->crossings().size(), 2u);
}
