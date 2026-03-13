#include <gtest/gtest.h>
#include "editor/visual/container/linear_layout.h"
#include "editor/visual/container/container.h"
#include "editor/visual/render_context.h"

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
