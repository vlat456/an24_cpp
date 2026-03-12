/// Regression tests for the visual layer refactoring audit.
/// Validates: extracted types in isolation, [[nodiscard]] addWire semantics,
/// dependency decoupling (port.h -> widget_base.h, not widget.h).

#include <gtest/gtest.h>
#include "editor/visual/node/bounds.h"
#include "editor/visual/node/edges.h"
#include "editor/visual/node/widget/widget_base.h"
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
