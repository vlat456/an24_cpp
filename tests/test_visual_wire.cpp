#include "ui/math/pt.h"

using ui::Pt;

#include <gtest/gtest.h>
#include "editor/visual/widget.h"
#include "ui/core/grid.h"
#include "editor/visual/scene.h"
#include "editor/visual/wire/wire.h"
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
    bool isClickable() const override { return true; }

    /// Override portByName so Wire can resolve endpoints
    visual::Port* portByName(std::string_view port_name,
                             std::string_view /*wire_id*/ = {}) const override {
        for (auto& child : children()) {
            if (auto* port = dynamic_cast<visual::Port*>(child.get())) {
                if (port->name() == port_name) return port;
            }
        }
        return nullptr;
    }

private:
    std::string id_;
};

/// Helper: set up a scene with two fake nodes that have ports,
/// and create a wire between them.
struct WireTestFixture {
    visual::Scene scene;
    visual::Port* pa_ptr = nullptr;
    visual::Port* pb_ptr = nullptr;
    visual::Wire* wire_ptr = nullptr;

    /// Creates nodes "a" at pos_a and "b" at pos_b with ports "out" and "in",
    /// and a wire "w1" connecting a.out -> b.in
    void setup(Pt pos_a, Pt port_a_local,
               Pt pos_b, Pt port_b_local) {
        auto node_a = std::make_unique<FakeNode>("a", pos_a);
        auto port_a = std::make_unique<visual::Port>("out", PortSide::Output, PortType::V);
        port_a->setLocalPos(port_a_local);
        pa_ptr = port_a.get();
        node_a->addChild(std::move(port_a));
        scene.add(std::move(node_a));

        auto node_b = std::make_unique<FakeNode>("b", pos_b);
        auto port_b = std::make_unique<visual::Port>("in", PortSide::Input, PortType::V);
        port_b->setLocalPos(port_b_local);
        pb_ptr = port_b.get();
        node_b->addChild(std::move(port_b));
        scene.add(std::move(node_b));

        auto wire = std::make_unique<visual::Wire>("w1", "a", "out", "b", "in");
        wire_ptr = wire.get();
        scene.add(std::move(wire));
    }
};

// ============================================================
// Construction & Properties
// ============================================================

TEST(WireTest, Construction) {
    visual::Wire wire("w1", "a", "out", "b", "in");

    EXPECT_EQ(wire.startEndpoint().node_id, "a");
    EXPECT_EQ(wire.startEndpoint().port_name, "out");
    EXPECT_EQ(wire.endEndpoint().node_id, "b");
    EXPECT_EQ(wire.endEndpoint().port_name, "in");
}

TEST(WireTest, Id) {
    visual::Wire wire("wire_42", "a", "out", "b", "in");
    EXPECT_EQ(wire.id(), "wire_42");
}

TEST(WireTest, WireIsClickable) {
    visual::Wire wire("w", "a", "out", "b", "in");
    EXPECT_TRUE(wire.isClickable());
}

TEST(WireTest, RoutingPointIsClickable) {
    visual::RoutingPoint rp(Pt(10, 20));
    EXPECT_TRUE(rp.isClickable());
}

// ============================================================
// Polyline
// ============================================================

TEST(WireTest, PolylineBasic) {
    WireTestFixture f;
    f.setup(Pt(0, 0), Pt(100, 30), Pt(200, 0), Pt(0, 30));

    auto pl = f.wire_ptr->polyline();
    ASSERT_EQ(pl.size(), 2u);
    // start world = node_a(0,0) + port(100,30) + (RADIUS,RADIUS)
    EXPECT_FLOAT_EQ(pl[0].x, 104.0f);
    EXPECT_FLOAT_EQ(pl[0].y, 34.0f);
    // end world = node_b(200,0) + port(0,30) + (RADIUS,RADIUS)
    EXPECT_FLOAT_EQ(pl[1].x, 204.0f);
    EXPECT_FLOAT_EQ(pl[1].y, 34.0f);
}

TEST(WireTest, PolylineWithRouting) {
    WireTestFixture f;
    f.setup(Pt(0, 0), Pt(100, 30), Pt(200, 0), Pt(0, 30));
    f.wire_ptr->addRoutingPoint(Pt(150, 50), 0);

    auto pl = f.wire_ptr->polyline();
    ASSERT_EQ(pl.size(), 3u);
    EXPECT_FLOAT_EQ(pl[0].x, 104.0f);   // start (port center)
    EXPECT_FLOAT_EQ(pl[1].x, 150.0f);   // routing point
    EXPECT_FLOAT_EQ(pl[1].y, 50.0f);
    EXPECT_FLOAT_EQ(pl[2].x, 204.0f);   // end (port center)
}

TEST(WireTest, PolylineUnresolvableEndpoints) {
    // Wire with no scene — endpoints cannot resolve
    visual::Wire wire("w1", "a", "out", "b", "in");
    auto pl = wire.polyline();
    EXPECT_TRUE(pl.empty());
}

TEST(WireTest, PolylineOneEndResolvable) {
    visual::Scene scene;

    auto node_a = std::make_unique<FakeNode>("a", Pt(0, 0));
    auto port_a = std::make_unique<visual::Port>("out", PortSide::Output, PortType::V);
    port_a->setLocalPos(Pt(100, 30));
    node_a->addChild(std::move(port_a));
    scene.add(std::move(node_a));

    // No node "b" in scene — end endpoint unresolvable
    auto wire = std::make_unique<visual::Wire>("w1", "a", "out", "b", "in");
    auto* w = wire.get();
    scene.add(std::move(wire));

    auto pl = w->polyline();
    ASSERT_EQ(pl.size(), 1u);
    EXPECT_FLOAT_EQ(pl[0].x, 104.0f);
    EXPECT_FLOAT_EQ(pl[0].y, 34.0f);
}

// ============================================================
// Bounding Box (virtual worldMin/worldMax override)
// ============================================================

TEST(WireTest, BoundsFromPolyline) {
    WireTestFixture f;
    f.setup(Pt(100, 100), Pt(0, 0), Pt(300, 200), Pt(0, 0));

    // Polyline: (104,104) -> (304,204) — port center offsets
    // worldMin = (104-4, 104-4) = (100, 100)
    // worldMax = (304+4, 204+4) = (308, 208)
    Pt mn = f.wire_ptr->worldMin();
    Pt mx = f.wire_ptr->worldMax();
    EXPECT_FLOAT_EQ(mn.x, 100.0f);
    EXPECT_FLOAT_EQ(mn.y, 100.0f);
    EXPECT_FLOAT_EQ(mx.x, 308.0f);
    EXPECT_FLOAT_EQ(mx.y, 208.0f);
}

TEST(WireTest, BoundsEmptyPolyline) {
    visual::Wire wire("w", "a", "out", "b", "in");
    Pt mn = wire.worldMin();
    Pt mx = wire.worldMax();
    EXPECT_FLOAT_EQ(mn.x, 0.0f);
    EXPECT_FLOAT_EQ(mn.y, 0.0f);
    EXPECT_FLOAT_EQ(mx.x, 0.0f);
    EXPECT_FLOAT_EQ(mx.y, 0.0f);
}

TEST(WireTest, BoundsVirtualDispatch) {
    // Verify Grid sees the overridden worldMin/worldMax via Widget*
    WireTestFixture f;
    f.setup(Pt(100, 100), Pt(0, 0), Pt(300, 200), Pt(0, 0));

    visual::Widget* w = f.wire_ptr; // base pointer
    Pt mn = w->worldMin();
    EXPECT_FLOAT_EQ(mn.x, 100.0f);
    EXPECT_FLOAT_EQ(mn.y, 100.0f);
}

// ============================================================
// Routing Points
// ============================================================

TEST(WireTest, AddRoutingPoint) {
    visual::Wire wire("w", "a", "out", "b", "in");
    auto* rp = wire.addRoutingPoint(Pt(50, 60), 0);
    EXPECT_NE(rp, nullptr);
    EXPECT_EQ(wire.children().size(), 1u);
}

TEST(WireTest, AddRoutingPointOrdered) {
    visual::Wire wire("w", "a", "out", "b", "in");
    wire.addRoutingPoint(Pt(10, 0), 0);
    wire.addRoutingPoint(Pt(30, 0), 1);
    wire.addRoutingPoint(Pt(20, 0), 1); // insert between

    ASSERT_EQ(wire.children().size(), 3u);
    EXPECT_FLOAT_EQ(wire.children()[0]->localPos().x, 10.0f);
    EXPECT_FLOAT_EQ(wire.children()[1]->localPos().x, 20.0f);
    EXPECT_FLOAT_EQ(wire.children()[2]->localPos().x, 30.0f);
}

TEST(WireTest, RemoveRoutingPoint) {
    visual::Wire wire("w", "a", "out", "b", "in");
    wire.addRoutingPoint(Pt(50, 60), 0);
    EXPECT_EQ(wire.children().size(), 1u);
    wire.removeRoutingPoint(0);
    EXPECT_EQ(wire.children().size(), 0u);
}

// ============================================================
// Scene Integration
// ============================================================

TEST(WireTest, SceneIntegration) {
    WireTestFixture f;
    f.setup(Pt(100, 100), Pt(0, 0), Pt(300, 200), Pt(0, 0));

    EXPECT_NE(f.scene.find("w1"), nullptr);

    // Wire should be queryable in Grid near its endpoint
    auto results = f.scene.grid().queryAs<visual::Wire>(Pt(100, 100), 10.0f);
    EXPECT_GE(results.size(), 1u);
}

TEST(WireTest, WireRemovedWhenNodeRemoved) {
    WireTestFixture f;
    f.setup(Pt(0, 0), Pt(100, 30), Pt(200, 0), Pt(0, 30));

    EXPECT_EQ(f.scene.roots().size(), 3u); // node_a + node_b + wire

    // Remove node_a — wire still exists (it's a root widget, not a child)
    f.scene.remove(f.scene.find("a"));
    f.scene.flushRemovals();

    // Wire is NOT auto-removed anymore (no WireEnd cascade).
    // Wire still exists but its start endpoint can't resolve.
    EXPECT_NE(f.scene.find("w1"), nullptr);
    auto pl = f.wire_ptr->polyline();
    // Only end resolves (node_b still alive)
    ASSERT_EQ(pl.size(), 1u);
}

TEST(WireTest, RenderNoCrash) {
    visual::Wire wire("w", "a", "out", "b", "in");
    visual::RenderContext ctx;
    ctx.zoom = 1.0f;
    EXPECT_NO_FATAL_FAILURE(wire.render(nullptr, ctx));
}

// ============================================================
// Crossing Detection & Arc/Gap Rendering
// ============================================================

/// Helper: create a Wire with no resolvable endpoints but with routing points
/// that define a polyline. Wire localPos stays at (0,0).
static visual::Wire* makePolylineWire(visual::Scene& scene,
                                       const std::string& id,
                                       const std::vector<Pt>& pts) {
    auto wire = std::make_unique<visual::Wire>(id, "", "", "", "");
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
    visual::Scene scene;

    auto* w1 = makePolylineWire(scene, "w1", {{0, 64}, {128, 64}});
    auto* w2 = makePolylineWire(scene, "w2", {{64, 0}, {64, 128}});

    visual::compute_wire_crossings(scene);

    ASSERT_EQ(w1->crossings().size(), 1u);
    ASSERT_EQ(w2->crossings().size(), 1u);
    EXPECT_NEAR(w1->crossings()[0].pos.x, 64.0f, 0.5f);
    EXPECT_NEAR(w1->crossings()[0].pos.y, 64.0f, 0.5f);
}

TEST(WireTest, CrossingDetected_ParallelNoCross) {
    visual::Scene scene;

    auto* w1 = makePolylineWire(scene, "w1", {{0, 50}, {100, 50}});
    auto* w2 = makePolylineWire(scene, "w2", {{0, 80}, {100, 80}});

    visual::compute_wire_crossings(scene);

    EXPECT_EQ(w1->crossings().size(), 0u);
    EXPECT_EQ(w2->crossings().size(), 0u);
}

TEST(WireTest, CrossingArcGapAssignment) {
    visual::Scene scene;

    auto* w1 = makePolylineWire(scene, "w1", {{0, 50}, {100, 50}});
    auto* w2 = makePolylineWire(scene, "w2", {{50, 0}, {50, 100}});

    visual::compute_wire_crossings(scene);

    ASSERT_EQ(w1->crossings().size(), 1u);
    ASSERT_EQ(w2->crossings().size(), 1u);

    EXPECT_FALSE(w1->crossings()[0].draw_arc);
    EXPECT_TRUE(w2->crossings()[0].draw_arc);
}

TEST(WireTest, CrossingDetected_MultipleIntersections) {
    visual::Scene scene;

    auto* w1 = makePolylineWire(scene, "w1", {{0, 50}, {200, 50}});
    auto* w2 = makePolylineWire(scene, "w2", {{30, 0}, {30, 100}, {100, 100}, {100, 0}});

    visual::compute_wire_crossings(scene);

    EXPECT_EQ(w1->crossings().size(), 2u);
    EXPECT_EQ(w2->crossings().size(), 2u);
}
