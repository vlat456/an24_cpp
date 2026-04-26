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
auto port_a = std::make_unique<visual::Port>("out", bp2::Direction::Output, PortType::V);
        port_a->setLocalPos(port_a_local);
        pa_ptr = port_a.get();
        node_a->addChild(std::move(port_a));
        scene.add(std::move(node_a));

        auto node_b = std::make_unique<FakeNode>("b", pos_b);
        auto port_b = std::make_unique<visual::Port>("in", bp2::Direction::Input, PortType::V);
        port_b->setLocalPos(port_b_local);
        pb_ptr = port_b.get();
        node_b->addChild(std::move(port_b));
        scene.add(std::move(node_b));

auto wire = std::make_unique<visual::Wire>(core::InternedId{}, "w1", "a", "out", "b", "in");
        wire_ptr = wire.get();
        scene.add(std::move(wire));
    }
};

// ============================================================
// Construction & Properties
// ============================================================

TEST(WireTest, Construction) {
    visual::Wire wire(core::InternedId{}, "w1", "a", "out", "b", "in");

    EXPECT_EQ(wire.startEndpoint().node_id, "a");
    EXPECT_EQ(wire.startEndpoint().port_name, "out");
    EXPECT_EQ(wire.endEndpoint().node_id, "b");
    EXPECT_EQ(wire.endEndpoint().port_name, "in");
}

TEST(WireTest, Id) {
    visual::Wire wire(core::InternedId{}, "wire_42", "a", "out", "b", "in");
    EXPECT_EQ(wire.id(), "wire_42");
}

TEST(WireTest, WireIsClickable) {
    visual::Wire wire(core::InternedId{}, "w", "a", "out", "b", "in");
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
    constexpr float R = visual::PortConstants::RADIUS;
    // Endpoints are offset from port center toward the adjacent polyline point:
    //   Source offset = R - 0.5 = 2.5 (slight overlap into circle)
    //   Dest offset   = R + 1.0 = 4.0 (gap so arrowhead doesn't overlap circle)
    // This is a horizontal wire, so only x changes.
    constexpr float SRC_OFFSET = R - 0.5f;
    constexpr float DST_OFFSET = R + 1.0f;
    // start world = node_a(0,0) + port(100,30) + (R,R), then +SRC_OFFSET in x
    EXPECT_FLOAT_EQ(pl[0].x, 100.0f + R + SRC_OFFSET);
    EXPECT_FLOAT_EQ(pl[0].y, 30.0f + R);
    // end world = node_b(200,0) + port(0,30) + (R,R), then -DST_OFFSET in x
    EXPECT_FLOAT_EQ(pl[1].x, 200.0f + R - DST_OFFSET);
    EXPECT_FLOAT_EQ(pl[1].y, 30.0f + R);
}

TEST(WireTest, PolylineWithRouting) {
    WireTestFixture f;
    f.setup(Pt(0, 0), Pt(100, 30), Pt(200, 0), Pt(0, 30));
    f.wire_ptr->addRoutingPoint(Pt(150, 50), 0);

    constexpr float R = visual::PortConstants::RADIUS;
    auto pl = f.wire_ptr->polyline();
    ASSERT_EQ(pl.size(), 3u);
    // Endpoints are offset from port center toward the adjacent point.
    // Source is offset toward the routing point (not horizontal), so use NEAR.
    constexpr float SRC_OFFSET = R - 0.5f;
    constexpr float DST_OFFSET = R + 1.0f;
    // Start center = (103, 33), next = (150, 50) — non-axis-aligned offset
    EXPECT_NEAR(pl[0].x, 100.0f + R, SRC_OFFSET + 0.1f);
    EXPECT_GT(pl[0].x, 100.0f + R);  // shifted toward routing point
    EXPECT_FLOAT_EQ(pl[1].x, 150.0f);   // routing point unchanged
    EXPECT_FLOAT_EQ(pl[1].y, 50.0f);
    // End center = (203, 33), prev = (150, 50) — non-axis-aligned offset
    EXPECT_NEAR(pl[2].x, 200.0f + R, DST_OFFSET + 0.1f);
    EXPECT_LT(pl[2].x, 200.0f + R);  // shifted toward routing point
}

TEST(WireTest, PolylineUnresolvableEndpoints) {
    // Wire with no scene — endpoints cannot resolve
    visual::Wire wire(core::InternedId{}, "w1", "a", "out", "b", "in");
    auto pl = wire.polyline();
    EXPECT_TRUE(pl.empty());
}

TEST(WireTest, PolylineOneEndResolvable) {
    visual::Scene scene;

    auto node_a = std::make_unique<FakeNode>("a", Pt(0, 0));
        auto port_a = std::make_unique<visual::Port>("out", bp2::Direction::Output, PortType::V);
    port_a->setLocalPos(Pt(100, 30));
    node_a->addChild(std::move(port_a));
    scene.add(std::move(node_a));

    // No node "b" in scene — end endpoint unresolvable
    auto wire = std::make_unique<visual::Wire>(core::InternedId{}, "w1", "a", "out", "b", "in");
    auto* w = wire.get();
    scene.add(std::move(wire));

    auto pl = w->polyline();
    ASSERT_EQ(pl.size(), 1u);
    constexpr float R = visual::PortConstants::RADIUS;
    EXPECT_FLOAT_EQ(pl[0].x, 100.0f + R);
    EXPECT_FLOAT_EQ(pl[0].y, 30.0f + R);
}

// ============================================================
// Bounding Box (virtual worldMin/worldMax override)
// ============================================================

TEST(WireTest, BoundsFromPolyline) {
    WireTestFixture f;
    f.setup(Pt(100, 100), Pt(0, 0), Pt(300, 200), Pt(0, 0));

    constexpr float R = visual::PortConstants::RADIUS;
    constexpr float PAD = 4.0f;  // Wire::BBOX_PADDING
    // Polyline endpoints are offset from port center toward adjacent point.
    // The exact offset depends on the segment direction (non-axis-aligned here),
    // so bounds shift slightly inward compared to port centers.
    Pt mn = f.wire_ptr->worldMin();
    Pt mx = f.wire_ptr->worldMax();
    // Start center = (100+R, 100+R), End center = (300+R, 200+R)
    // Both are offset inward, so bounds are within [center - PAD, center + PAD]
    EXPECT_GT(mn.x, 100.0f + R - PAD - 0.5f);
    EXPECT_GT(mn.y, 100.0f + R - PAD - 0.5f);
    EXPECT_LT(mx.x, 300.0f + R + PAD + 0.5f);
    EXPECT_LT(mx.y, 200.0f + R + PAD + 0.5f);
    // Bounds should still be sane (min < max)
    EXPECT_LT(mn.x, mx.x);
    EXPECT_LT(mn.y, mx.y);
}

TEST(WireTest, BoundsEmptyPolyline) {
    visual::Wire wire(core::InternedId{}, "w", "a", "out", "b", "in");
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
    // Virtual dispatch must return the same values as the derived pointer
    Pt mn = w->worldMin();
    Pt mx = w->worldMax();
    Pt mn_derived = f.wire_ptr->worldMin();
    Pt mx_derived = f.wire_ptr->worldMax();
    EXPECT_FLOAT_EQ(mn.x, mn_derived.x);
    EXPECT_FLOAT_EQ(mn.y, mn_derived.y);
    EXPECT_FLOAT_EQ(mx.x, mx_derived.x);
    EXPECT_FLOAT_EQ(mx.y, mx_derived.y);
    // Sanity: bounds are reasonable (min < max, roughly around port centers)
    EXPECT_LT(mn.x, mx.x);
    EXPECT_LT(mn.y, mx.y);
}

// ============================================================
// Routing Points
// ============================================================

TEST(WireTest, AddRoutingPoint) {
    visual::Wire wire(core::InternedId{}, "w", "a", "out", "b", "in");
    auto* rp = wire.addRoutingPoint(Pt(50, 60), 0);
    EXPECT_NE(rp, nullptr);
    EXPECT_EQ(wire.children().size(), 1u);
}

TEST(WireTest, AddRoutingPointOrdered) {
    visual::Wire wire(core::InternedId{}, "w", "a", "out", "b", "in");
    wire.addRoutingPoint(Pt(10, 0), 0);
    wire.addRoutingPoint(Pt(30, 0), 1);
    wire.addRoutingPoint(Pt(20, 0), 1); // insert between

    ASSERT_EQ(wire.children().size(), 3u);
    EXPECT_FLOAT_EQ(wire.children()[0]->localPos().x, 10.0f);
    EXPECT_FLOAT_EQ(wire.children()[1]->localPos().x, 20.0f);
    EXPECT_FLOAT_EQ(wire.children()[2]->localPos().x, 30.0f);
}

TEST(WireTest, RemoveRoutingPoint) {
    visual::Wire wire(core::InternedId{}, "w", "a", "out", "b", "in");
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
    visual::Wire wire(core::InternedId{}, "w", "a", "out", "b", "in");
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
    auto wire = std::make_unique<visual::Wire>(core::InternedId{}, id, "", "", "", "");
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

// ============================================================
// REGRESSION: Fix 4 — Epsilon-based movement detection
// ============================================================
// Before this fix, Wire::rebuildGeometry() compared float positions
// with exact equality. Tiny floating-point rounding errors (< 0.05)
// from layout or zoom operations could cause the wire to never
// detect that its endpoints had moved, making it permanently stale.
// The fix uses epsilon tolerance (0.05f) for the comparison.

TEST(WireTest, REGRESSION_EpsilonMovementDetection) {
    WireTestFixture f;
    f.setup(Pt(0, 0), Pt(100, 30), Pt(200, 0), Pt(0, 30));
    
    // Force initial geometry build
    auto pl1 = f.wire_ptr->polyline();
    ASSERT_GE(pl1.size(), 2u);
    float orig_start_x = pl1[0].x;
    
    // Move node_a by a tiny amount BELOW the epsilon threshold (0.05)
    // This should NOT trigger a rebuild
    auto* node_a = f.scene.find("a");
    ASSERT_NE(node_a, nullptr);
    node_a->setLocalPos(Pt(0.02f, 0.0f));
    
    auto pl2 = f.wire_ptr->polyline();
    // The wire should return the CACHED polyline (not rebuilt)
    // because the movement is below the 0.05 epsilon threshold
    EXPECT_FLOAT_EQ(pl2[0].x, orig_start_x)
        << "Movement below epsilon should not trigger rebuild";
    
    // Now move node_a by a larger amount ABOVE the epsilon threshold
    node_a->setLocalPos(Pt(1.0f, 0.0f));
    
    auto pl3 = f.wire_ptr->polyline();
    // The wire should have rebuilt — start position should change
    EXPECT_NE(pl3[0].x, orig_start_x)
        << "Movement above epsilon should trigger rebuild";
}

// ============================================================
// REGRESSION: Fix 2 — dynamic_cast filter for non-Wire roots
// ============================================================
// Before this fix, compute_wire_crossings() used static_cast<Wire*>
// on all roots with RenderLayer::Wire, which is UB if non-Wire
// widgets ever have that render layer. The fix uses dynamic_cast
// and filters out non-Wire widgets safely.

TEST(WireTest, REGRESSION_CrossingDetectionWithNonWireRoots) {
    visual::Scene scene;
    
    // Add a non-Wire clickable widget to the scene
    // (e.g., a FakeNode — it has RenderLayer::Normal, but
    //  the point is that the scene has mixed widget types)
    auto node = std::make_unique<FakeNode>("n1", Pt(25, 25));
    scene.add(std::move(node));
    
    // Add two crossing wires
    auto* w1 = makePolylineWire(scene, "w1", {{0, 50}, {100, 50}});
    auto* w2 = makePolylineWire(scene, "w2", {{50, 0}, {50, 100}});
    
    // This should NOT crash even with non-Wire roots in the scene
    EXPECT_NO_FATAL_FAILURE(visual::compute_wire_crossings(scene));
    
    // Crossings should still be detected correctly
    EXPECT_EQ(w1->crossings().size(), 1u);
    EXPECT_EQ(w2->crossings().size(), 1u);
}

// ============================================================
// REGRESSION: Wire invalidateGeometry forces fresh rebuild
// ============================================================

TEST(WireTest, REGRESSION_InvalidateGeometryForcesRebuild) {
    WireTestFixture f;
    f.setup(Pt(0, 0), Pt(100, 30), Pt(200, 0), Pt(0, 30));
    
    // Force initial build
    auto pl1 = f.wire_ptr->polyline();
    ASSERT_GE(pl1.size(), 2u);
    
    // Add a routing point — this calls invalidateGeometry()
    f.wire_ptr->addRoutingPoint(Pt(150, 80), 0);
    
    auto pl2 = f.wire_ptr->polyline();
    // After invalidation, polyline must include the routing point
    ASSERT_EQ(pl2.size(), 3u);
    EXPECT_FLOAT_EQ(pl2[1].x, 150.0f);
    EXPECT_FLOAT_EQ(pl2[1].y, 80.0f);
}
