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
    void render(IDrawList*, const RenderContext&) const override {}
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
