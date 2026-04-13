#include <gtest/gtest.h>
#include "editor/visual/container/linear_layout.h"
#include "editor/visual/container/container.h"
#include "editor/visual/render_context.h"

using ui::Pt;
using ui::Edges;

namespace visual {

class LeafWidget : public Widget {
public:
    LeafWidget(Pt sz) { setSize(sz); }
    Pt preferredSize(IDrawList*) const override { return size(); }
    Pt minimumSize(IDrawList*) const override { return size(); }
    void render(IDrawList*, const RenderContext&) const override {}
};

class OverflowLeafWidget : public Widget {
public:
    OverflowLeafWidget(Pt preferred, Pt minimum)
        : preferred_(preferred), minimum_(minimum) {}

    Pt preferredSize(IDrawList*) const override { return preferred_; }
    Pt minimumSize(IDrawList*) const override { return minimum_; }
    void render(IDrawList*, const RenderContext&) const override {}

private:
    Pt preferred_;
    Pt minimum_;
};

} // namespace visual

TEST(LinearLayoutTest, RowLayout) {
    visual::Row row;
    row.emplaceChild<visual::LeafWidget>(Pt(50, 30));
    row.emplaceChild<visual::LeafWidget>(Pt(30, 30));
    
    row.layout(100, 30);
    
    EXPECT_EQ(row.children()[0]->localPos().x, 0);
    EXPECT_EQ(row.children()[1]->localPos().x, 50);
}

TEST(LinearLayoutTest, ColumnLayout) {
    visual::Column col;
    col.emplaceChild<visual::LeafWidget>(Pt(50, 30));
    col.emplaceChild<visual::LeafWidget>(Pt(50, 20));
    
    col.layout(50, 60);
    
    EXPECT_EQ(col.children()[0]->localPos().y, 0);
    EXPECT_EQ(col.children()[1]->localPos().y, 30);
}

TEST(LinearLayoutTest, FlexibleChild) {
    visual::Row row;
    auto* flex_child = row.emplaceChild<visual::LeafWidget>(Pt(50, 30));
    flex_child->setFlexible(true);
    
    row.layout(100, 30);
    
    EXPECT_EQ(row.children()[0]->size().x, 100);
}

TEST(LinearLayoutTest, PreferredSize) {
    visual::Row row;
    row.emplaceChild<visual::LeafWidget>(Pt(50, 30));
    row.emplaceChild<visual::LeafWidget>(Pt(30, 40));
    
    Pt ps = row.preferredSize(nullptr);
    
    EXPECT_EQ(ps.x, 80);
    EXPECT_EQ(ps.y, 40);
}

TEST(LinearLayoutTest, MixedFlexibleAndFixed) {
    visual::Row row;
    row.emplaceChild<visual::LeafWidget>(Pt(30, 20));
    auto* flex = row.emplaceChild<visual::LeafWidget>(Pt(10, 20));
    flex->setFlexible(true);
    row.emplaceChild<visual::LeafWidget>(Pt(30, 20));
    
    row.layout(100, 20);
    
    EXPECT_EQ(row.children()[0]->localPos().x, 0);
    EXPECT_EQ(row.children()[0]->size().x, 30);
    EXPECT_EQ(row.children()[1]->localPos().x, 30);
    EXPECT_EQ(row.children()[1]->size().x, 40);
    EXPECT_EQ(row.children()[2]->localPos().x, 70);
    EXPECT_EQ(row.children()[2]->size().x, 30);
}

TEST(ContainerTest, Margins) {
    visual::Container container(Edges::all(10));
    container.emplaceChild<visual::LeafWidget>(Pt(50, 30));
    
    container.layout(100, 60);
    
    EXPECT_EQ(container.children()[0]->localPos().x, 10);
    EXPECT_EQ(container.children()[0]->localPos().y, 10);
    EXPECT_EQ(container.children()[0]->size().x, 80);
    EXPECT_EQ(container.children()[0]->size().y, 40);
}

TEST(ContainerTest, PreferredSize) {
    visual::Container container(Edges::all(5));
    container.emplaceChild<visual::LeafWidget>(Pt(50, 30));
    
    Pt ps = container.preferredSize(nullptr);
    
    EXPECT_EQ(ps.x, 60);
    EXPECT_EQ(ps.y, 40);
}

TEST(ContainerTest, EmptyContainer) {
    visual::Container container;
    
    Pt ps = container.preferredSize(nullptr);
    
    EXPECT_EQ(ps.x, 0);
    EXPECT_EQ(ps.y, 0);
}

// ============================================================
// REGRESSION: Fix 3 — Container margins exceeding available space
// ============================================================
// Before this fix, Container::layout() would compute negative dimensions
// (available - margins) when margins exceeded the available space, passing
// them to the child's layout(). This caused undefined layout behavior.
// The fix clamps child dimensions to zero via std::max(0.0f, ...).

TEST(ContainerTest, REGRESSION_MarginsExceedAvailableSpace) {
    // Margins: 60 left + 60 right = 120, but available width is only 80
    visual::Container container(Edges{60, 60, 60, 60});
    container.emplaceChild<visual::LeafWidget>(Pt(50, 30));
    
    container.layout(80, 80);
    
    // Child should receive clamped-to-zero dimensions, NOT negative values
    Pt child_size = container.children()[0]->size();
    EXPECT_GE(child_size.x, 0.0f) << "Child width must never be negative";
    EXPECT_GE(child_size.y, 0.0f) << "Child height must never be negative";
    EXPECT_FLOAT_EQ(child_size.x, 0.0f);
    EXPECT_FLOAT_EQ(child_size.y, 0.0f);
    
    // Child position should still be at margin offsets
    EXPECT_FLOAT_EQ(container.children()[0]->localPos().x, 60.0f);
    EXPECT_FLOAT_EQ(container.children()[0]->localPos().y, 60.0f);
}

TEST(ContainerTest, REGRESSION_MarginsExactlyEqualSpace) {
    // Edge case: margins consume exactly all available space
    // Edges constructor: left, top, right, bottom
    visual::Container container(Edges{50, 25, 50, 25});
    container.emplaceChild<visual::LeafWidget>(Pt(50, 30));
    
    container.layout(100, 50);
    
    Pt child_size = container.children()[0]->size();
    EXPECT_FLOAT_EQ(child_size.x, 0.0f);
    EXPECT_FLOAT_EQ(child_size.y, 0.0f);
}

TEST(LinearLayoutTest, WorldPosThroughRow) {
    visual::Row row;
    row.setLocalPos(Pt(100, 200));
    auto* child = row.emplaceChild<visual::LeafWidget>(Pt(50, 30));
    
    EXPECT_EQ(child->worldPos().x, 100);
    EXPECT_EQ(child->worldPos().y, 200);
    
    row.layout(50, 30);
    
    EXPECT_EQ(child->worldPos().x, 100);
    EXPECT_EQ(child->worldPos().y, 200);
}

TEST(LinearLayoutTest, MinimumSizeIncludesFlexibleChildReserve) {
    visual::Row row;
    row.emplaceChild<visual::LeafWidget>(Pt(30, 20));
    auto* center = row.emplaceChild<visual::LeafWidget>(Pt(40, 20));
    center->setFlexGrow(1.0f);
    row.emplaceChild<visual::LeafWidget>(Pt(30, 20));

    Pt preferred = row.preferredSize(nullptr);
    Pt minimum = row.minimumSize(nullptr);

    EXPECT_FLOAT_EQ(preferred.x, 60.0f);
    EXPECT_FLOAT_EQ(minimum.x, 100.0f);
    EXPECT_FLOAT_EQ(minimum.y, 20.0f);
}

TEST(LinearLayoutTest, ColumnMinimumWidthUsesChildMinimumNotPreferredOverflow) {
    visual::Column column;
    column.emplaceChild<visual::OverflowLeafWidget>(Pt(120, 16), Pt(0, 16));
    column.emplaceChild<visual::LeafWidget>(Pt(48, 20));

    Pt preferred = column.preferredSize(nullptr);
    Pt minimum = column.minimumSize(nullptr);

    EXPECT_FLOAT_EQ(preferred.x, 120.0f);
    EXPECT_FLOAT_EQ(minimum.x, 48.0f);
    EXPECT_FLOAT_EQ(minimum.y, 36.0f);
}

// ============================================================
// REGRESSION: Header label must constrain node minimum width
// ============================================================
// Simulates a Column with a "header-like" child (min == preferred, i.e.
// identity text that must always be visible) and a "footer-like" child
// (wide preferred but zero minimum, i.e. decorative text that can clip).
// The Column's minimum width must be at least the header's width, while
// the footer must NOT inflate it.

TEST(LinearLayoutTest, REGRESSION_HeaderConstrainsColumnMinWidth_FooterDoesNot) {
    visual::Column column;

    // Header-like child: preferred width 80, minimum width 80 (identity text)
    column.emplaceChild<visual::OverflowLeafWidget>(Pt(80, 24), Pt(80, 24));

    // Port row: preferred width 60
    column.emplaceChild<visual::LeafWidget>(Pt(60, 20));

    // Footer-like child: preferred width 120, minimum width 0 (decorative)
    column.emplaceChild<visual::OverflowLeafWidget>(Pt(120, 16), Pt(0, 16));

    Pt preferred = column.preferredSize(nullptr);
    Pt minimum = column.minimumSize(nullptr);

    // Preferred width = max of children preferred = 120 (footer)
    EXPECT_FLOAT_EQ(preferred.x, 120.0f);

    // Minimum width = max of children minimum = 80 (header), NOT 0 (footer)
    EXPECT_FLOAT_EQ(minimum.x, 80.0f)
        << "Header identity text must prevent node from shrinking below its width";

    // Minimum height = sum of all children minimum heights
    EXPECT_FLOAT_EQ(minimum.y, 60.0f);  // 24 + 20 + 16
}

// ============================================================
// REGRESSION: Zero-minimum child does NOT inflate column minimum
// ============================================================
// Verifies that a child with wide preferred size but zero minimum
// width does not contribute to the Column's cross-axis minimum.

TEST(LinearLayoutTest, REGRESSION_ZeroMinimumChildDoesNotInflateColumnMinWidth) {
    visual::Column column;

    // Narrow fixed child (e.g. port row)
    column.emplaceChild<visual::LeafWidget>(Pt(48, 20));

    // Wide decorative child with zero minimum width
    column.emplaceChild<visual::OverflowLeafWidget>(Pt(200, 16), Pt(0, 16));

    Pt minimum = column.minimumSize(nullptr);

    EXPECT_FLOAT_EQ(minimum.x, 48.0f)
        << "Decorative child with zero minimum must not widen column minimum";
}

// ============================================================
// linearLayout shrink: non-flex children shrink toward minimum
// ============================================================
// When the available main-axis space is less than the sum of preferred
// sizes, non-flex children should shrink proportionally from their
// preferred toward their minimum — not overflow.

TEST(LinearLayoutTest, ShrinkNonFlexChildrenProportionally) {
    visual::Column column;

    // Child A: preferred height 30, minimum height 10 (shrinkable by 20)
    column.emplaceChild<visual::OverflowLeafWidget>(Pt(50, 30), Pt(50, 10));

    // Child B: preferred height 30, minimum height 10 (shrinkable by 20)
    column.emplaceChild<visual::OverflowLeafWidget>(Pt(50, 30), Pt(50, 10));

    // Total preferred = 60, total minimum = 20, available = 40.
    // Need to shrink by 20 out of a budget of 40 → 50% shrink fraction.
    // Each child: 30 - (30-10)*0.5 = 30 - 10 = 20.
    column.layout(50, 40);

    EXPECT_FLOAT_EQ(column.children()[0]->size().y, 20.0f);
    EXPECT_FLOAT_EQ(column.children()[1]->size().y, 20.0f);
    EXPECT_FLOAT_EQ(column.children()[1]->localPos().y, 20.0f);
}

TEST(LinearLayoutTest, ShrinkFullyToMinimumWhenSpaceVeryTight) {
    visual::Column column;

    // Child A: preferred height 30, minimum height 10
    column.emplaceChild<visual::OverflowLeafWidget>(Pt(50, 30), Pt(50, 10));

    // Child B: preferred height 30, minimum height 10
    column.emplaceChild<visual::OverflowLeafWidget>(Pt(50, 30), Pt(50, 10));

    // Total preferred = 60, total minimum = 20, available = 15 (below minimum).
    // Shrink fraction = min(1.0, (60-15)/(60-20)) = min(1.0, 1.125) = 1.0.
    // Each child shrinks fully to minimum = 10.
    column.layout(50, 15);

    EXPECT_FLOAT_EQ(column.children()[0]->size().y, 10.0f);
    EXPECT_FLOAT_EQ(column.children()[1]->size().y, 10.0f);
    EXPECT_FLOAT_EQ(column.children()[1]->localPos().y, 10.0f);
}

TEST(LinearLayoutTest, ShrinkDoesNotAffectFlexChildren) {
    visual::Row row;

    // Fixed child: preferred width 60, minimum width 20
    row.emplaceChild<visual::OverflowLeafWidget>(Pt(60, 20), Pt(20, 20));

    // Flex child
    auto* flex = row.emplaceChild<visual::LeafWidget>(Pt(10, 20));
    flex->setFlexGrow(1.0f);

    // Available = 50, fixed preferred = 60 → needs shrink.
    // Fixed child shrinks from 60 toward 20. Need=10, budget=40 → 25%.
    // Fixed child: 60 - 40*0.25 = 50. Flex child: 0 (no surplus).
    row.layout(50, 20);

    EXPECT_FLOAT_EQ(row.children()[0]->size().x, 50.0f);
    EXPECT_FLOAT_EQ(row.children()[1]->size().x, 0.0f);
}
