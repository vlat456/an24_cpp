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
