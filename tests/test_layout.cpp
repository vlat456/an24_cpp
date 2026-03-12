#include <gtest/gtest.h>
#include <cmath>
#include "editor/visual/node/widget/widget_base.h"
#include "editor/visual/node/widget/containers/column.h"
#include "editor/visual/node/widget/containers/row.h"
#include "editor/visual/node/widget/containers/container.h"
#include "editor/visual/node/widget/primitives/label.h"
#include "editor/visual/node/widget/primitives/circle.h"
#include "editor/visual/node/widget/primitives/spacer.h"
#include "editor/visual/node/edges.h"
#include "editor/visual/renderer/mock_draw_list.h"

// ============================================================================
// Helper: concrete leaf widget for testing (fixed size, not flexible)
// ============================================================================
class FixedWidget : public Widget {
public:
    FixedWidget(float w, float h) { width_ = w; height_ = h; }
    Pt getPreferredSize(IDrawList*) const override { return Pt(width_, height_); }
    void render(IDrawList*, Pt, float) const override {}
};

class FlexWidget : public Widget {
public:
    FlexWidget() { flexible_ = true; }
    Pt getPreferredSize(IDrawList*) const override { return Pt(0, 0); }
    void render(IDrawList*, Pt, float) const override {}
};

// ============================================================================
// Edges struct
// ============================================================================

TEST(EdgesTest, DefaultIsZero) {
    Edges e;
    EXPECT_FLOAT_EQ(e.left, 0.0f);
    EXPECT_FLOAT_EQ(e.top, 0.0f);
    EXPECT_FLOAT_EQ(e.right, 0.0f);
    EXPECT_FLOAT_EQ(e.bottom, 0.0f);
}

TEST(EdgesTest, NamedConstructors) {
    Edges a = Edges::all(5.0f);
    EXPECT_FLOAT_EQ(a.left, 5.0f);
    EXPECT_FLOAT_EQ(a.top, 5.0f);
    EXPECT_FLOAT_EQ(a.right, 5.0f);
    EXPECT_FLOAT_EQ(a.bottom, 5.0f);

    Edges s = Edges::symmetric(10.0f, 5.0f);
    EXPECT_FLOAT_EQ(s.left, 10.0f);
    EXPECT_FLOAT_EQ(s.right, 10.0f);
    EXPECT_FLOAT_EQ(s.top, 5.0f);
    EXPECT_FLOAT_EQ(s.bottom, 5.0f);
}

// ============================================================================
// Column — vertical stack
// ============================================================================

TEST(ColumnTest, Empty_PreferredSizeIsZero) {
    Column col;
    Pt ps = col.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 0.0f);
    EXPECT_FLOAT_EQ(ps.y, 0.0f);
}

TEST(ColumnTest, SingleChild_PreferredSizeEqualsChild) {
    Column col;
    col.addChild(std::make_unique<FixedWidget>(100, 50));
    Pt ps = col.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 100.0f);
    EXPECT_FLOAT_EQ(ps.y, 50.0f);
}

TEST(ColumnTest, TwoChildren_WidthIsMax_HeightIsSum) {
    Column col;
    col.addChild(std::make_unique<FixedWidget>(80, 30));
    col.addChild(std::make_unique<FixedWidget>(120, 40));
    Pt ps = col.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 120.0f);  // max(80, 120)
    EXPECT_FLOAT_EQ(ps.y, 70.0f);   // 30 + 40
}

TEST(ColumnTest, Layout_FixedChildrenGetPreferredHeight) {
    Column col;
    auto* a = col.addChild(std::make_unique<FixedWidget>(80, 30));
    auto* b = col.addChild(std::make_unique<FixedWidget>(60, 40));

    col.layout(200, 200);

    // Both get full width
    EXPECT_FLOAT_EQ(a->width(), 200.0f);
    EXPECT_FLOAT_EQ(b->width(), 200.0f);
    // Heights = preferred
    EXPECT_FLOAT_EQ(a->height(), 30.0f);
    EXPECT_FLOAT_EQ(b->height(), 40.0f);
    // Stacked: a at y=0, b at y=30
    EXPECT_FLOAT_EQ(a->y(), 0.0f);
    EXPECT_FLOAT_EQ(b->y(), 30.0f);
}

TEST(ColumnTest, Layout_FlexibleChildTakesRemaining) {
    Column col;
    auto* fixed = col.addChild(std::make_unique<FixedWidget>(80, 30));
    auto* flex  = col.addChild(std::make_unique<FlexWidget>());

    col.layout(100, 100);

    EXPECT_FLOAT_EQ(fixed->height(), 30.0f);
    EXPECT_FLOAT_EQ(flex->height(), 70.0f);  // 100 - 30
    EXPECT_FLOAT_EQ(flex->y(), 30.0f);
}

TEST(ColumnTest, Layout_TwoFlexChildrenShareEqually) {
    Column col;
    auto* a = col.addChild(std::make_unique<FlexWidget>());
    auto* b = col.addChild(std::make_unique<FlexWidget>());

    col.layout(100, 80);

    EXPECT_FLOAT_EQ(a->height(), 40.0f);
    EXPECT_FLOAT_EQ(b->height(), 40.0f);
    EXPECT_FLOAT_EQ(a->y(), 0.0f);
    EXPECT_FLOAT_EQ(b->y(), 40.0f);
}

TEST(ColumnTest, ChildCount) {
    Column col;
    EXPECT_EQ(col.childCount(), 0u);
    col.addChild(std::make_unique<FixedWidget>(10, 10));
    col.addChild(std::make_unique<FixedWidget>(10, 10));
    EXPECT_EQ(col.childCount(), 2u);
}

TEST(ColumnTest, ChildAccess) {
    Column col;
    auto* ptr = col.addChild(std::make_unique<FixedWidget>(42, 17));
    EXPECT_EQ(col.child(0), ptr);
    EXPECT_EQ(col.child(1), nullptr);
}

// ============================================================================
// Row — horizontal stack
// ============================================================================

TEST(RowTest, Empty_PreferredSizeIsZero) {
    Row row;
    Pt ps = row.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 0.0f);
    EXPECT_FLOAT_EQ(ps.y, 0.0f);
}

TEST(RowTest, SingleChild_PreferredSizeEqualsChild) {
    Row row;
    row.addChild(std::make_unique<FixedWidget>(100, 50));
    Pt ps = row.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 100.0f);
    EXPECT_FLOAT_EQ(ps.y, 50.0f);
}

TEST(RowTest, TwoChildren_WidthIsSum_HeightIsMax) {
    Row row;
    row.addChild(std::make_unique<FixedWidget>(80, 30));
    row.addChild(std::make_unique<FixedWidget>(120, 40));
    Pt ps = row.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 200.0f);  // 80 + 120
    EXPECT_FLOAT_EQ(ps.y, 40.0f);   // max(30, 40)
}

TEST(RowTest, Layout_FixedChildrenGetPreferredWidth) {
    Row row;
    auto* a = row.addChild(std::make_unique<FixedWidget>(80, 30));
    auto* b = row.addChild(std::make_unique<FixedWidget>(60, 40));

    row.layout(300, 50);

    // Both get full height
    EXPECT_FLOAT_EQ(a->height(), 50.0f);
    EXPECT_FLOAT_EQ(b->height(), 50.0f);
    // Widths = preferred
    EXPECT_FLOAT_EQ(a->width(), 80.0f);
    EXPECT_FLOAT_EQ(b->width(), 60.0f);
    // Side by side: a at x=0, b at x=80
    EXPECT_FLOAT_EQ(a->x(), 0.0f);
    EXPECT_FLOAT_EQ(b->x(), 80.0f);
}

TEST(RowTest, Layout_FlexibleChildTakesRemaining) {
    Row row;
    auto* fixed = row.addChild(std::make_unique<FixedWidget>(80, 30));
    auto* flex  = row.addChild(std::make_unique<FlexWidget>());

    row.layout(200, 50);

    EXPECT_FLOAT_EQ(fixed->width(), 80.0f);
    EXPECT_FLOAT_EQ(flex->width(), 120.0f);  // 200 - 80
    EXPECT_FLOAT_EQ(flex->x(), 80.0f);
}

TEST(RowTest, Layout_TwoFlexChildrenShareEqually) {
    Row row;
    auto* a = row.addChild(std::make_unique<FlexWidget>());
    auto* b = row.addChild(std::make_unique<FlexWidget>());

    row.layout(100, 50);

    EXPECT_FLOAT_EQ(a->width(), 50.0f);
    EXPECT_FLOAT_EQ(b->width(), 50.0f);
    EXPECT_FLOAT_EQ(a->x(), 0.0f);
    EXPECT_FLOAT_EQ(b->x(), 50.0f);
}

TEST(RowTest, ChildCount) {
    Row row;
    EXPECT_EQ(row.childCount(), 0u);
    row.addChild(std::make_unique<FixedWidget>(10, 10));
    EXPECT_EQ(row.childCount(), 1u);
}

// ============================================================================
// Container — single child with margins
// ============================================================================

TEST(ContainerTest, NoChild_PreferredIsMarginsOnly) {
    Container c(nullptr, Edges::all(10.0f));
    Pt ps = c.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 20.0f);  // 10 + 10
    EXPECT_FLOAT_EQ(ps.y, 20.0f);
}

TEST(ContainerTest, WithChild_PreferredIsChildPlusMargins) {
    Container c(std::make_unique<FixedWidget>(80, 40), Edges{5, 10, 15, 20});
    Pt ps = c.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 100.0f);  // 5 + 80 + 15
    EXPECT_FLOAT_EQ(ps.y, 70.0f);   // 10 + 40 + 20
}

TEST(ContainerTest, NegativeMargins_ShrinkPreferredSize) {
    // Port circle (r=6) with -6 left margin: exposed width = 12 - 6 = 6
    Container c(std::make_unique<FixedWidget>(12, 12), Edges{-6, 0, 0, 0});
    Pt ps = c.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 6.0f);   // -6 + 12 + 0
    EXPECT_FLOAT_EQ(ps.y, 12.0f);  // 0 + 12 + 0
}

TEST(ContainerTest, Layout_ChildPositionedAtMarginOffset) {
    auto child_ptr = std::make_unique<FixedWidget>(80, 40);
    auto* child = child_ptr.get();
    Container c(std::move(child_ptr), Edges{5, 10, 15, 20});

    c.layout(200, 100);

    // Child positioned at (left_margin, top_margin)
    EXPECT_FLOAT_EQ(child->x(), 5.0f);
    EXPECT_FLOAT_EQ(child->y(), 10.0f);
    // Child gets available - margins
    EXPECT_FLOAT_EQ(child->width(), 180.0f);   // 200 - 5 - 15
    EXPECT_FLOAT_EQ(child->height(), 70.0f);   // 100 - 10 - 20
}

TEST(ContainerTest, Layout_NegativeMargin_ChildExtendsOutside) {
    auto child_ptr = std::make_unique<FixedWidget>(12, 12);
    auto* child = child_ptr.get();
    Container c(std::move(child_ptr), Edges{-6, 0, 0, 0});

    c.layout(50, 50);

    // Child starts at x=-6 (extends 6px LEFT of container)
    EXPECT_FLOAT_EQ(child->x(), -6.0f);
    EXPECT_FLOAT_EQ(child->y(), 0.0f);
    // Child width = 50 - (-6) - 0 = 56
    EXPECT_FLOAT_EQ(child->width(), 56.0f);
    EXPECT_FLOAT_EQ(child->height(), 50.0f);
}

TEST(ContainerTest, ZeroMargins_ChildFillsContainer) {
    auto child_ptr = std::make_unique<FixedWidget>(80, 40);
    auto* child = child_ptr.get();
    Container c(std::move(child_ptr), Edges{});

    c.layout(200, 100);

    EXPECT_FLOAT_EQ(child->x(), 0.0f);
    EXPECT_FLOAT_EQ(child->y(), 0.0f);
    EXPECT_FLOAT_EQ(child->width(), 200.0f);
    EXPECT_FLOAT_EQ(child->height(), 100.0f);
}

// ============================================================================
// Label — self-sizing text
// ============================================================================

TEST(LabelTest, PreferredSize_UsesDrawList) {
    MockDrawList dl;
    Label lbl("Hello", 12.0f);
    Pt ps = lbl.getPreferredSize(&dl);
    // MockDrawList: text_width = strlen * font_size * 0.6 = 5 * 12 * 0.6 = 36
    EXPECT_FLOAT_EQ(ps.x, 36.0f);
    EXPECT_FLOAT_EQ(ps.y, 12.0f);
}

TEST(LabelTest, PreferredSize_FallbackWithoutDrawList) {
    Label lbl("Hello", 12.0f);
    Pt ps = lbl.getPreferredSize(nullptr);
    // Fallback: strlen * font_size * 0.6 = 36
    EXPECT_FLOAT_EQ(ps.x, 36.0f);
    EXPECT_FLOAT_EQ(ps.y, 12.0f);
}

TEST(LabelTest, EmptyString_PreferredSizeIsZeroWidth) {
    Label lbl("", 12.0f);
    Pt ps = lbl.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 0.0f);
    EXPECT_FLOAT_EQ(ps.y, 12.0f);
}

TEST(LabelTest, Render_DrawsText) {
    MockDrawList dl;
    Label lbl("Hi", 10.0f, 0xFF00FF00);
    lbl.layout(100, 20);
    lbl.render(&dl, Pt(50, 100), 2.0f);

    ASSERT_EQ(dl.texts_.size(), 1u);
    EXPECT_EQ(dl.texts_[0].text, "Hi");
    EXPECT_EQ(dl.texts_[0].color, 0xFF00FF00u);
}

TEST(LabelTest, NotFlexible) {
    Label lbl("text", 10.0f);
    EXPECT_FALSE(lbl.isFlexible());
}

// ============================================================================
// Circle — fixed-size port indicator
// ============================================================================

TEST(CircleTest, PreferredSize_IsDiameter) {
    Circle c(6.0f, 0xFFFFFFFF);
    Pt ps = c.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 12.0f);
    EXPECT_FLOAT_EQ(ps.y, 12.0f);
}

TEST(CircleTest, Render_DrawsFilledCircle) {
    MockDrawList dl;
    Circle c(6.0f, 0xFFFF0000);
    c.layout(12, 12);
    c.render(&dl, Pt(100, 200), 1.0f);

    EXPECT_TRUE(dl.had_circle_);
    ASSERT_FALSE(dl.circle_colors_.empty());
    EXPECT_EQ(dl.circle_colors_[0], 0xFFFF0000u);
}

TEST(CircleTest, NotFlexible) {
    Circle c(6.0f, 0xFFFFFFFF);
    EXPECT_FALSE(c.isFlexible());
}

// ============================================================================
// Spacer — flexible empty space
// ============================================================================

TEST(SpacerTest, PreferredSizeIsZero) {
    Spacer s;
    Pt ps = s.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 0.0f);
    EXPECT_FLOAT_EQ(ps.y, 0.0f);
}

TEST(SpacerTest, IsFlexible) {
    Spacer s;
    EXPECT_TRUE(s.isFlexible());
}

TEST(SpacerTest, Render_NoCrash) {
    MockDrawList dl;
    Spacer s;
    s.layout(100, 50);
    s.render(&dl, Pt(0, 0), 1.0f);
    // Should not crash, should not draw anything
    EXPECT_FALSE(dl.had_rect_);
    EXPECT_FALSE(dl.had_circle_);
    EXPECT_TRUE(dl.texts_.empty());
}

// ============================================================================
// Nesting — Column { Row { ... } }
// ============================================================================

TEST(NestingTest, RowInsideColumn) {
    // Simulate a port row: Circle(6) | Label("v_in") | Spacer | Label("v_out") | Circle(6)
    auto row = std::make_unique<Row>();
    row->addChild(std::make_unique<Circle>(6.0f, 0xFFFFFFFF));         // 12x12
    row->addChild(std::make_unique<FixedWidget>(30, 9));               // label placeholder
    row->addChild(std::make_unique<Spacer>());                          // flexible
    row->addChild(std::make_unique<FixedWidget>(40, 9));               // label placeholder
    row->addChild(std::make_unique<Circle>(6.0f, 0xFFFFFFFF));         // 12x12

    Pt row_ps = row->getPreferredSize(nullptr);
    // Width: 12 + 30 + 0 + 40 + 12 = 94
    EXPECT_FLOAT_EQ(row_ps.x, 94.0f);
    // Height: max(12, 9, 0, 9, 12) = 12
    EXPECT_FLOAT_EQ(row_ps.y, 12.0f);

    Column col;
    col.addChild(std::make_unique<FixedWidget>(100, 24));  // header
    auto* row_ptr = col.addChild(std::move(row));
    col.addChild(std::make_unique<FixedWidget>(80, 16));   // type name

    Pt col_ps = col.getPreferredSize(nullptr);
    // Width: max(100, 94, 80) = 100
    EXPECT_FLOAT_EQ(col_ps.x, 100.0f);
    // Height: 24 + 12 + 16 = 52
    EXPECT_FLOAT_EQ(col_ps.y, 52.0f);

    col.layout(120, 52);

    // Row gets full width, preferred height
    EXPECT_FLOAT_EQ(row_ptr->width(), 120.0f);
    EXPECT_FLOAT_EQ(row_ptr->height(), 12.0f);
    EXPECT_FLOAT_EQ(row_ptr->y(), 24.0f);
}

TEST(NestingTest, ContainerWithNegativeMarginInRow) {
    // Port circle hanging 6px to the left via negative margin
    auto port = std::make_unique<Container>(
        std::make_unique<Circle>(6.0f, 0xFFFFFFFF),
        Edges{-6, 0, 0, 0}
    );

    Pt ps = port->getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(ps.x, 6.0f);   // 12 + (-6) + 0 = 6
    EXPECT_FLOAT_EQ(ps.y, 12.0f);

    Row row;
    auto* port_ptr = row.addChild(std::move(port));
    row.addChild(std::make_unique<FixedWidget>(60, 10));  // label

    Pt row_ps = row.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(row_ps.x, 66.0f);  // 6 + 60
    EXPECT_FLOAT_EQ(row_ps.y, 12.0f);  // max(12, 10)

    row.layout(66, 12);

    // Container at x=0, width=6. Its child (Circle) at x=-6 inside container.
    EXPECT_FLOAT_EQ(port_ptr->x(), 0.0f);
    EXPECT_FLOAT_EQ(port_ptr->width(), 6.0f);
}

TEST(NestingTest, DeepNesting_ColumnRowColumn) {
    // Column { Row { Column { Fixed } } }
    auto inner_col = std::make_unique<Column>();
    inner_col->addChild(std::make_unique<FixedWidget>(30, 20));
    inner_col->addChild(std::make_unique<FixedWidget>(40, 25));

    Pt inner_ps = inner_col->getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(inner_ps.x, 40.0f);
    EXPECT_FLOAT_EQ(inner_ps.y, 45.0f);

    auto row = std::make_unique<Row>();
    row->addChild(std::make_unique<FixedWidget>(10, 50));
    row->addChild(std::move(inner_col));

    Pt row_ps = row->getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(row_ps.x, 50.0f);   // 10 + 40
    EXPECT_FLOAT_EQ(row_ps.y, 50.0f);   // max(50, 45)

    Column outer;
    outer.addChild(std::move(row));

    Pt outer_ps = outer.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(outer_ps.x, 50.0f);
    EXPECT_FLOAT_EQ(outer_ps.y, 50.0f);
}

// ============================================================================
// Re-layout: calling layout() again repositions children
// ============================================================================

TEST(ColumnTest, Relayout_UpdatesPositions) {
    Column col;
    auto* a = col.addChild(std::make_unique<FixedWidget>(80, 30));
    auto* b = col.addChild(std::make_unique<FixedWidget>(60, 40));

    col.layout(100, 100);
    EXPECT_FLOAT_EQ(b->y(), 30.0f);

    // Re-layout with different height shouldn't change fixed positions
    col.layout(200, 200);
    EXPECT_FLOAT_EQ(a->y(), 0.0f);
    EXPECT_FLOAT_EQ(b->y(), 30.0f);
    EXPECT_FLOAT_EQ(a->width(), 200.0f);
    EXPECT_FLOAT_EQ(b->width(), 200.0f);
}

TEST(RowTest, Relayout_UpdatesPositions) {
    Row row;
    auto* a = row.addChild(std::make_unique<FixedWidget>(80, 30));
    auto* b = row.addChild(std::make_unique<FixedWidget>(60, 40));

    row.layout(200, 50);
    EXPECT_FLOAT_EQ(b->x(), 80.0f);

    row.layout(300, 80);
    EXPECT_FLOAT_EQ(a->x(), 0.0f);
    EXPECT_FLOAT_EQ(b->x(), 80.0f);
    EXPECT_FLOAT_EQ(a->height(), 80.0f);
    EXPECT_FLOAT_EQ(b->height(), 80.0f);
}

// ============================================================================
// Render pass-through: containers forward render to children
// ============================================================================

TEST(ColumnTest, Render_ForwardsToChildren) {
    MockDrawList dl;
    Column col;
    col.addChild(std::make_unique<Label>("A", 10.0f, 0xFFFFFFFF));
    col.addChild(std::make_unique<Label>("B", 10.0f, 0xFFFFFFFF));
    col.layout(100, 40);
    col.render(&dl, Pt(0, 0), 1.0f);
    EXPECT_EQ(dl.texts_.size(), 2u);
}

TEST(RowTest, Render_ForwardsToChildren) {
    MockDrawList dl;
    Row row;
    row.addChild(std::make_unique<Label>("X", 10.0f, 0xFFFFFFFF));
    row.addChild(std::make_unique<Label>("Y", 10.0f, 0xFFFFFFFF));
    row.layout(100, 20);
    row.render(&dl, Pt(0, 0), 1.0f);
    EXPECT_EQ(dl.texts_.size(), 2u);
}

TEST(ContainerTest, Render_ForwardsToChild) {
    MockDrawList dl;
    Container c(std::make_unique<Label>("Z", 10.0f, 0xFFFFFFFF), Edges::all(5.0f));
    c.layout(100, 30);
    c.render(&dl, Pt(0, 0), 1.0f);
    EXPECT_EQ(dl.texts_.size(), 1u);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(ColumnTest, Layout_NotEnoughSpace_FixedChildrenStillGetPreferred) {
    // Available < sum of preferred — fixed children keep their sizes
    Column col;
    auto* a = col.addChild(std::make_unique<FixedWidget>(80, 60));
    auto* b = col.addChild(std::make_unique<FixedWidget>(80, 60));

    col.layout(80, 50);  // Only 50 available, but children need 120

    EXPECT_FLOAT_EQ(a->height(), 60.0f);
    EXPECT_FLOAT_EQ(b->height(), 60.0f);
    EXPECT_FLOAT_EQ(b->y(), 60.0f);  // Still stacked
}

TEST(RowTest, Layout_NotEnoughSpace_FixedChildrenStillGetPreferred) {
    Row row;
    auto* a = row.addChild(std::make_unique<FixedWidget>(80, 30));
    auto* b = row.addChild(std::make_unique<FixedWidget>(80, 30));

    row.layout(50, 30);  // Only 50 available, but children need 160

    EXPECT_FLOAT_EQ(a->width(), 80.0f);
    EXPECT_FLOAT_EQ(b->width(), 80.0f);
    EXPECT_FLOAT_EQ(b->x(), 80.0f);
}

TEST(ContainerTest, NullChild_NoCrash) {
    Container c(nullptr, Edges::all(10.0f));
    c.layout(100, 100);
    MockDrawList dl;
    c.render(&dl, Pt(0, 0), 1.0f);  // Should not crash
}
