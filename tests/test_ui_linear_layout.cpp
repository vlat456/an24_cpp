#include "ui/layout/linear_layout.h"
#include "ui/core/widget.h"
#include <gtest/gtest.h>

namespace {
class FixedWidget : public ui::Widget {
public:
    ui::Pt pref_size;
    FixedWidget(ui::Pt size) : pref_size(size) {}
    ui::Pt preferredSize(ui::IDrawList*) const override { return pref_size; }
};

class FlexWidget : public ui::Widget {
public:
    FlexWidget() { setFlexible(true); }
};
}

TEST(UILayout, LinearLayoutHorizontal) {
    ui::LinearLayout<ui::Axis::Horizontal> layout;
    
    auto a = std::make_unique<FixedWidget>(ui::Pt{50, 100});
    auto b = std::make_unique<FixedWidget>(ui::Pt{30, 80});
    layout.addChild(std::move(a));
    layout.addChild(std::move(b));
    
    layout.layout(200, 100);
    
    EXPECT_FLOAT_EQ(layout.size().x, 200.0f);
    EXPECT_FLOAT_EQ(layout.size().y, 100.0f);
}

TEST(UILayout, LinearLayoutVertical) {
    ui::LinearLayout<ui::Axis::Vertical> layout;
    
    auto a = std::make_unique<FixedWidget>(ui::Pt{100, 50});
    auto b = std::make_unique<FixedWidget>(ui::Pt{80, 30});
    layout.addChild(std::move(a));
    layout.addChild(std::move(b));
    
    layout.layout(100, 200);
    
    EXPECT_FLOAT_EQ(layout.size().x, 100.0f);
    EXPECT_FLOAT_EQ(layout.size().y, 200.0f);
}

TEST(UILayout, LinearLayoutFlex) {
    ui::LinearLayout<ui::Axis::Horizontal> layout;
    
    auto fixed = std::make_unique<FixedWidget>(ui::Pt{50, 100});
    auto flex = std::make_unique<FlexWidget>();
    
    layout.addChild(std::move(fixed));
    layout.addChild(std::move(flex));
    
    layout.layout(200, 100);
    
    // Fixed takes 50, flex takes remaining 150
    EXPECT_FLOAT_EQ(layout.children()[0]->localPos().x, 0.0f);
    EXPECT_FLOAT_EQ(layout.children()[1]->localPos().x, 50.0f);
}

TEST(UILayout, RowAndColumnTypeAliases) {
    ui::Row row;
    ui::Column column;
    
    SUCCEED();
}
