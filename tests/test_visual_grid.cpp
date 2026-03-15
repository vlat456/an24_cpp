#include "ui/math/pt.h"

using ui::Pt;

#include <gtest/gtest.h>
#include "editor/visual/widget.h"
#include "ui/core/grid.h"
#include "editor/visual/render_context.h"

namespace visual {

class ClickableWidget : public Widget {
public:
    ClickableWidget(Pt pos, Pt sz) {
        local_pos_ = pos;
        size_ = sz;
    }
    bool isClickable() const override { return true; }
    void render(IDrawList*, const RenderContext&) const override {}
};

} // namespace visual

TEST(GridTest, EmptyGrid) {
    ui::Grid g;
    auto result = g.query(Pt(0, 0), 10.0f);
    EXPECT_TRUE(result.empty());
}

TEST(GridTest, InsertWidget) {
    ui::Grid g;
    visual::ClickableWidget w(Pt(100.0f, 100.0f), Pt(50.0f, 50.0f));
    
    g.insert(&w);
    auto result = g.query(Pt(110.0f, 110.0f), 10.0f);
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], &w);
}

TEST(GridTest, RemoveWidget) {
    ui::Grid g;
    visual::ClickableWidget w(Pt(100.0f, 100.0f), Pt(50.0f, 50.0f));
    
    g.insert(&w);
    g.remove(&w);
    auto result = g.query(Pt(110.0f, 110.0f), 10.0f);
    EXPECT_TRUE(result.empty());
}

TEST(GridTest, UpdateWidget) {
    ui::Grid g;
    visual::ClickableWidget w(Pt(100.0f, 100.0f), Pt(50.0f, 50.0f));
    
    g.insert(&w);
    w.setLocalPos(Pt(200.0f, 200.0f));
    g.update(&w);
    
    auto oldResult = g.query(Pt(110.0f, 110.0f), 10.0f);
    EXPECT_TRUE(oldResult.empty());
    
    auto newResult = g.query(Pt(210.0f, 210.0f), 10.0f);
    EXPECT_EQ(newResult.size(), 1u);
}

TEST(GridTest, QueryWithMargin) {
    ui::Grid g;
    visual::ClickableWidget w(Pt(100.0f, 100.0f), Pt(50.0f, 50.0f));
    
    g.insert(&w);
    
    auto result = g.query(Pt(50.0f, 100.0f), 60.0f);
    EXPECT_EQ(result.size(), 1u);
}

TEST(GridTest, QueryOutsideMargin) {
    ui::Grid g;
    visual::ClickableWidget w(Pt(100.0f, 100.0f), Pt(50.0f, 50.0f));
    
    g.insert(&w);
    
    // Grid uses cell-based indexing, so query with very small margin
    // may still return widgets from nearby cells. Use larger distance.
    auto result = g.query(Pt(10.0f, 10.0f), 5.0f);
    EXPECT_TRUE(result.empty());
}

TEST(GridTest, MultipleWidgets) {
    ui::Grid g;
    visual::ClickableWidget w1(Pt(100.0f, 100.0f), Pt(50.0f, 50.0f));
    visual::ClickableWidget w2(Pt(200.0f, 200.0f), Pt(50.0f, 50.0f));
    
    g.insert(&w1);
    g.insert(&w2);
    
    auto result = g.query(Pt(110.0f, 110.0f), 10.0f);
    EXPECT_EQ(result.size(), 1u);
    
    auto result2 = g.query(Pt(210.0f, 210.0f), 10.0f);
    EXPECT_EQ(result2.size(), 1u);
}

TEST(GridTest, ClearGrid) {
    ui::Grid g;
    visual::ClickableWidget w(Pt(100.0f, 100.0f), Pt(50.0f, 50.0f));
    
    g.insert(&w);
    g.clear();
    
    auto result = g.query(Pt(110.0f, 110.0f), 10.0f);
    EXPECT_TRUE(result.empty());
}

TEST(GridTest, QueryAsType) {
    ui::Grid g;
    visual::ClickableWidget w(Pt(100.0f, 100.0f), Pt(50.0f, 50.0f));
    
    g.insert(&w);
    
    auto result = g.queryAs<visual::Widget>(Pt(110.0f, 110.0f), 10.0f);
    EXPECT_EQ(result.size(), 1u);
}
