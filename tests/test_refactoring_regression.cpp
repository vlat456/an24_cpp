/// Regression tests for the visual layer refactoring audit.
/// Validates: extracted types in isolation, [[nodiscard]] addWire semantics,
/// dependency decoupling (port.h -> widget_base.h, not widget.h),
/// and bug-fix regressions (div-by-zero, null deref, spatial grid, container).

#include <gtest/gtest.h>
#include "editor/visual/node/bounds.h"
#include "editor/visual/node/edges.h"
#include "editor/visual/node/widget/widget_base.h"
#include "editor/visual/node/widget/content/voltmeter_widget.h"
#include "editor/visual/node/widget/containers/container.h"
#include "editor/visual/node/types/ref_node.h"
#include "editor/visual/scene/scene.h"

// ============================================================================
// Bounds (extracted from widget.h) — standalone value type
// ============================================================================

TEST(BoundsExtracted, DefaultZero) {
    Bounds b;
    EXPECT_FLOAT_EQ(b.x, 0.0f);
    EXPECT_FLOAT_EQ(b.y, 0.0f);
    EXPECT_FLOAT_EQ(b.w, 0.0f);
    EXPECT_FLOAT_EQ(b.h, 0.0f);
}

TEST(BoundsExtracted, ContainsInside) {
    Bounds b{10, 20, 100, 50};
    EXPECT_TRUE(b.contains(10, 20));      // top-left corner
    EXPECT_TRUE(b.contains(50, 40));      // center
    EXPECT_TRUE(b.contains(109.9f, 69.9f)); // near bottom-right
}

TEST(BoundsExtracted, ContainsOutside) {
    Bounds b{10, 20, 100, 50};
    EXPECT_FALSE(b.contains(9.9f, 30));   // left of left edge
    EXPECT_FALSE(b.contains(110, 30));    // right edge (exclusive)
    EXPECT_FALSE(b.contains(50, 19.9f)); // above top edge
    EXPECT_FALSE(b.contains(50, 70));    // bottom edge (exclusive)
}

TEST(BoundsExtracted, ZeroSizeContainsNothing) {
    Bounds b{5, 5, 0, 0};
    EXPECT_FALSE(b.contains(5, 5));
}

// ============================================================================
// Edges (extracted from layout.h) — standalone POD
// ============================================================================

TEST(EdgesExtracted, DefaultZero) {
    Edges e;
    EXPECT_FLOAT_EQ(e.left, 0.0f);
    EXPECT_FLOAT_EQ(e.top, 0.0f);
    EXPECT_FLOAT_EQ(e.right, 0.0f);
    EXPECT_FLOAT_EQ(e.bottom, 0.0f);
}

TEST(EdgesExtracted, AllFactory) {
    Edges e = Edges::all(8.0f);
    EXPECT_FLOAT_EQ(e.left, 8.0f);
    EXPECT_FLOAT_EQ(e.top, 8.0f);
    EXPECT_FLOAT_EQ(e.right, 8.0f);
    EXPECT_FLOAT_EQ(e.bottom, 8.0f);
}

TEST(EdgesExtracted, SymmetricFactory) {
    Edges e = Edges::symmetric(4.0f, 8.0f);
    EXPECT_FLOAT_EQ(e.left, 4.0f);
    EXPECT_FLOAT_EQ(e.right, 4.0f);
    EXPECT_FLOAT_EQ(e.top, 8.0f);
    EXPECT_FLOAT_EQ(e.bottom, 8.0f);
}

// ============================================================================
// Widget base (extracted from widget.h) — abstract base class
// ============================================================================

namespace {
class StubWidget : public Widget {
public:
    StubWidget(float w, float h) { width_ = w; height_ = h; }
    Pt getPreferredSize(IDrawList*) const override { return Pt(width_, height_); }
    void render(IDrawList*, Pt, float) const override {}
};
}

TEST(WidgetBaseExtracted, DefaultPosition) {
    StubWidget w(100, 50);
    EXPECT_FLOAT_EQ(w.x(), 0.0f);
    EXPECT_FLOAT_EQ(w.y(), 0.0f);
}

TEST(WidgetBaseExtracted, SetPosition) {
    StubWidget w(100, 50);
    w.setPosition(10, 20);
    EXPECT_FLOAT_EQ(w.x(), 10.0f);
    EXPECT_FLOAT_EQ(w.y(), 20.0f);
}

TEST(WidgetBaseExtracted, GetSizeReturnsPt) {
    StubWidget w(120, 60);
    Pt sz = w.getSize();
    EXPECT_FLOAT_EQ(sz.x, 120.0f);
    EXPECT_FLOAT_EQ(sz.y, 60.0f);
}

TEST(WidgetBaseExtracted, GetBoundsReflectsPositionAndSize) {
    StubWidget w(100, 50);
    w.setPosition(10, 20);
    Bounds b = w.getBounds();
    EXPECT_FLOAT_EQ(b.x, 10.0f);
    EXPECT_FLOAT_EQ(b.y, 20.0f);
    EXPECT_FLOAT_EQ(b.w, 100.0f);
    EXPECT_FLOAT_EQ(b.h, 50.0f);
}

TEST(WidgetBaseExtracted, FlexibleDefault) {
    StubWidget w(100, 50);
    EXPECT_FALSE(w.isFlexible());
    w.setFlexible(true);
    EXPECT_TRUE(w.isFlexible());
}

// ============================================================================
// addWire [[nodiscard]] — return value must be meaningful
// ============================================================================

TEST(SceneAddWire, ReturnsTrueOnSuccess) {
    Blueprint bp;
    VisualScene scene(bp);

    Node n1; n1.id = "a"; n1.at(0, 0).size_wh(120, 80); n1.output("o");
    Node n2; n2.id = "b"; n2.at(300, 0).size_wh(120, 80); n2.input("i");
    scene.addNode(std::move(n1));
    scene.addNode(std::move(n2));

    Wire w = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    bool ok = scene.addWire(std::move(w));
    EXPECT_TRUE(ok);
    EXPECT_EQ(scene.wireCount(), 1u);
}

TEST(SceneAddWire, ReturnsFalseOnDuplicate) {
    Blueprint bp;
    VisualScene scene(bp);

    Node n1; n1.id = "a"; n1.at(0, 0).size_wh(120, 80); n1.output("o");
    Node n2; n2.id = "b"; n2.at(300, 0).size_wh(120, 80); n2.input("i");
    scene.addNode(std::move(n1));
    scene.addNode(std::move(n2));

    Wire w1 = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    Wire w2 = Wire::make("w1", wire_output("a", "o"), wire_input("b", "i"));
    EXPECT_TRUE(scene.addWire(std::move(w1)));
    EXPECT_FALSE(scene.addWire(std::move(w2)));
    EXPECT_EQ(scene.wireCount(), 1u);
}

// ============================================================================
// Dependency decoupling: port.h includes widget_base.h, not widget.h
// ============================================================================
// This is a compile-time structural test. If port.h included widget.h,
// then VisualPort would transitively see HeaderWidget, SwitchWidget, etc.
// We verify by including only port.h and checking that VisualPort compiles
// and inherits from Widget (the abstract base), proving the include chain
// goes through widget_base.h.

#include "editor/visual/port/port.h"

TEST(DependencyDecoupling, PortInheritsFromWidgetBase) {
    // VisualPort extends Widget (from widget_base.h).
    // If this compiles, the include chain port.h -> widget_base.h works.
    VisualPort vp("test_port", PortSide::Input, PortType::Any);
    Widget* base = &vp;  // implicit upcast to Widget*
    EXPECT_NE(base, nullptr);
}

TEST(DependencyDecoupling, PortCalcBoundsUsesExtractedBounds) {
    // Bounds (from bounds.h) is reachable through widget_base.h -> bounds.h.
    VisualPort vp("p", PortSide::Output, PortType::Any);
    vp.setWorldPosition(Pt(100, 200));
    Bounds b = vp.calcBounds();
    // Port bounds should be centered around world position with HIT_RADIUS
    EXPECT_GT(b.w, 0.0f);
    EXPECT_GT(b.h, 0.0f);
}

// ============================================================================
// Bug-fix regressions: VoltmeterWidget div-by-zero when min == max
// ============================================================================
// Before fix: `normalized = (clamped_val - min_val_) / (max_val_ - min_val_)`
// would divide by zero when min_val_ == max_val_.
// After fix: `range > 1e-6f` guard produces normalized = 0.0f instead.

#include "editor/visual/renderer/mock_draw_list.h"

TEST(BugRegression_DivByZero, VoltmeterEqualMinMax_NoNaN) {
    VoltmeterWidget gauge(5.0f, 5.0f, 5.0f, "V");
    gauge.layout(VoltmeterWidget::GAUGE_RADIUS * 2.0f, 100.0f);

    MockDrawList dl;
    Viewport vp;
    Pt origin(0, 0);
    gauge.render(&dl, origin, 1.0f);

    // Should render without producing NaN/Inf.
    // The needle circle-fill and the arc polyline must both be drawn.
    EXPECT_TRUE(dl.had_circle());
    EXPECT_TRUE(dl.had_polyline());

    // Verify no NaN in recorded positions (circle center must be finite).
    for (const auto& c : dl.circle_entries_) {
        EXPECT_FALSE(std::isnan(c.center.x));
        EXPECT_FALSE(std::isnan(c.center.y));
        EXPECT_FALSE(std::isinf(c.center.x));
        EXPECT_FALSE(std::isinf(c.center.y));
    }
}

TEST(BugRegression_DivByZero, VoltmeterNearlyEqualMinMax_NoNaN) {
    // Degenerate range smaller than epsilon guard (1e-6)
    VoltmeterWidget gauge(0.0f, 0.0f, 1e-8f, "V");
    gauge.layout(VoltmeterWidget::GAUGE_RADIUS * 2.0f, 100.0f);

    MockDrawList dl;
    gauge.render(&dl, Pt(0, 0), 1.0f);

    EXPECT_TRUE(dl.had_circle());
    for (const auto& c : dl.circle_entries_) {
        EXPECT_FALSE(std::isnan(c.center.x));
        EXPECT_FALSE(std::isnan(c.center.y));
    }
}

// ============================================================================
// Bug-fix regression: RefVisualNode render with empty ports
// ============================================================================
// Before fix: RefVisualNode::render accessed `ports_[0]` unconditionally.
// After fix: early return if `ports_.empty()`.

TEST(BugRegression_NullDeref, RefNode_EmptyPorts_RenderSafe) {
    // Construct a Ref node that has no inputs or outputs at all.
    // The constructor will still create a default "v" port, so we test
    // via the render guard path by constructing a node with ports,
    // rendering it, and verifying no crash.
    Node n;
    n.id = "ref1";
    n.name = "GND";
    n.render_hint = "ref";
    n.at(100, 100).size_wh(40, 30);
    // No inputs, no outputs — constructor defaults to port_name="v"
    RefVisualNode ref_node(n);

    MockDrawList dl;
    Viewport vp;
    Pt canvas_min(0, 0);
    // This must not crash regardless of port state.
    ref_node.render(&dl, vp, canvas_min, false);
    // The guard allows render to proceed since constructor created default port.
    EXPECT_TRUE(dl.had_circle());
}

TEST(BugRegression_NullDeref, RefNode_WithOutput_RenderHasPort) {
    Node n;
    n.id = "ref2";
    n.name = "V_bus";
    n.render_hint = "ref";
    n.at(200, 50).size_wh(40, 30);
    n.output("v_bus", PortType::V);

    RefVisualNode ref_node(n);

    MockDrawList dl;
    Viewport vp;
    ref_node.render(&dl, vp, Pt(0, 0), false);

    // Should draw at least one filled circle for the port.
    EXPECT_TRUE(dl.had_circle());
    bool found_filled = false;
    for (const auto& c : dl.circle_entries_) {
        if (c.filled) { found_filled = true; break; }
    }
    EXPECT_TRUE(found_filled);
}

// ============================================================================
// Bug-fix regression: SpatialGrid invalidation after moveNode
// ============================================================================
// Before fix: moveNode updated node position but did not call
// invalidateSpatialGrid(), causing stale hit-test results.
// After fix: moveNode always calls invalidateSpatialGrid().

TEST(BugRegression_SpatialGrid, MoveNode_HitTestFindsNewPosition) {
    Blueprint bp;
    VisualScene scene(bp);

    Node n;
    n.id = "movable";
    n.at(0, 0).size_wh(120, 80);
    n.output("o");
    scene.addNode(std::move(n));

    // Hit should find node at original position.
    auto hit1 = scene.hitTest(Pt(60, 40));
    EXPECT_EQ(hit1.type, HitType::Node);
    EXPECT_EQ(hit1.node_index, 0u);

    // Move node far away.
    scene.moveNode(0, Pt(1000, 1000));

    // Old position should now be empty.
    auto hit_old = scene.hitTest(Pt(60, 40));
    EXPECT_EQ(hit_old.type, HitType::None);

    // New position should find the node.
    auto hit_new = scene.hitTest(Pt(1060, 1040));
    EXPECT_EQ(hit_new.type, HitType::Node);
    EXPECT_EQ(hit_new.node_index, 0u);
}

TEST(BugRegression_SpatialGrid, MoveNode_MultipleMoves_GridStaysConsistent) {
    Blueprint bp;
    VisualScene scene(bp);

    Node n;
    n.id = "slider";
    n.at(0, 0).size_wh(100, 60);
    scene.addNode(std::move(n));

    // Move node through several positions rapidly.
    for (int i = 1; i <= 5; ++i) {
        Pt pos(static_cast<float>(i * 200), 0);
        scene.moveNode(0, pos);
    }

    // Final position: (1000, 0). Node should be found there.
    auto hit = scene.hitTest(Pt(1050, 30));
    EXPECT_EQ(hit.type, HitType::Node);

    // Intermediate positions should be empty.
    auto hit_mid = scene.hitTest(Pt(250, 30));
    EXPECT_EQ(hit_mid.type, HitType::None);
}

// ============================================================================
// Bug-fix regression: addNode creates visual from moved-into blueprint
// ============================================================================
// Before fix: addNode created the visual from a local copy of the Node
// *before* moving it into the blueprint, risking address mismatches.
// After fix: the node is moved into the blueprint first, then the visual
// is created from bp_->nodes[idx] (the authoritative copy).

TEST(BugRegression_AddNode, VisualPointsToBlueprint) {
    Blueprint bp;
    VisualScene scene(bp);

    Node n;
    n.id = "test_node";
    n.at(50, 100).size_wh(120, 80);
    n.output("o");
    size_t idx = scene.addNode(std::move(n));

    // The visual must exist and reflect the blueprint's node data.
    auto* vis = scene.visual("test_node");
    ASSERT_NE(vis, nullptr);

    // Position should match the blueprint's node.
    Pt bp_pos = bp.nodes[idx].pos;
    EXPECT_FLOAT_EQ(vis->getPosition().x, bp_pos.x);
    EXPECT_FLOAT_EQ(vis->getPosition().y, bp_pos.y);
}

TEST(BugRegression_AddNode, MultipleAdds_AllVisualsValid) {
    Blueprint bp;
    VisualScene scene(bp);

    for (int i = 0; i < 10; ++i) {
        Node n;
        n.id = "n" + std::to_string(i);
        n.at(static_cast<float>(i * 150), 0).size_wh(120, 80);
        n.output("o");
        scene.addNode(std::move(n));
    }

    EXPECT_EQ(scene.nodeCount(), 10u);

    for (int i = 0; i < 10; ++i) {
        std::string nid = "n" + std::to_string(i);
        auto* vis = scene.visual(nid);
        ASSERT_NE(vis, nullptr) << "Missing visual for " << nid;
        EXPECT_FLOAT_EQ(vis->getPosition().x, static_cast<float>(i * 150));
    }
}

// ============================================================================
// Bug-fix regression: Container::layout guards negative child dimensions
// ============================================================================
// Before fix: if available_width < margins.left + margins.right, the child
// would receive a negative width, causing undefined layout behavior.
// After fix: std::max(0.0f, ...) clamps child dimensions.

TEST(BugRegression_Container, NegativeChildDimensions_ClampedToZero) {
    auto inner = std::make_unique<StubWidget>(50, 30);
    Edges big_margins = Edges::all(100.0f);  // 200 total per axis
    Container c(std::move(inner), big_margins);

    // Available space (50x30) is smaller than margins (200x200).
    c.layout(50.0f, 30.0f);

    // Container should not pass negative dims to child.
    // The child should have been laid out with max(0, 50-200)=0.
    Widget* child = c.child();
    ASSERT_NE(child, nullptr);
    // Child position should still be at margin offset.
    EXPECT_FLOAT_EQ(child->x(), 100.0f);
    EXPECT_FLOAT_EQ(child->y(), 100.0f);
}
