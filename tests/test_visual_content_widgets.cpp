#include <gtest/gtest.h>
#include "editor/visual/widgets/content_widgets.h"
#include "editor/visual/container/container.h"
#include "editor/visual/container/linear_layout.h"
#include "visual/renderer/draw_list.h"

struct NodeContent {
    int type = 0;
    std::string label;
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    std::string unit;
    bool state = false;
    bool tripped = false;
};

namespace visual {

class MockDrawList : public ::IDrawList {
public:
    void add_line(Pt, Pt, uint32_t, float) {}
    void add_rect(Pt, Pt, uint32_t, float) {}
    void add_rect_with_rounding_corners(Pt, Pt, uint32_t, float, int, float) {}
    void add_rect_filled(Pt, Pt, uint32_t) {}
    void add_rect_filled_with_rounding(Pt, Pt, uint32_t, float) {}
    void add_rect_filled_with_rounding_corners(Pt, Pt, uint32_t, float, int) {}
    void add_circle(Pt, float, uint32_t, int) {}
    void add_circle_filled(Pt, float, uint32_t, int) {}
    void add_text(Pt, const char*, uint32_t, float) {}
    void add_polyline(const Pt*, size_t, uint32_t, float) {}
    Pt calc_text_size(const char* text, float font_size) const {
        float w = strlen(text) * font_size * 0.6f;
        return Pt(w, font_size);
    }
};

} // namespace visual

TEST(HeaderWidgetTest, PreferredSize) {
    visual::HeaderWidget header("TestNode", 0xFF404040);
    visual::MockDrawList dl;
    
    Pt ps = header.preferredSize(&dl);
    
    EXPECT_GT(ps.x, 0);
    EXPECT_EQ(ps.y, visual::HeaderWidget::HEIGHT);
}

TEST(HeaderWidgetTest, WorldPos) {
    visual::HeaderWidget header("Test", 0xFF404040);
    header.setLocalPos(Pt(100, 200));
    
    EXPECT_EQ(header.worldPos().x, 100);
    EXPECT_EQ(header.worldPos().y, 200);
}

TEST(HeaderWidgetTest, EstimateTextWidth) {
    float width = visual::HeaderWidget::estimateTextWidth("Hello");
    EXPECT_GT(width, 0);
    EXPECT_LT(width, 100);
}

TEST(TypeNameWidgetTest, PreferredSize) {
    visual::TypeNameWidget type_name("Battery");
    visual::MockDrawList dl;
    
    Pt ps = type_name.preferredSize(&dl);
    
    EXPECT_GT(ps.x, 0);
    EXPECT_EQ(ps.y, visual::TypeNameWidget::HEIGHT);
}

TEST(TypeNameWidgetTest, WorldPos) {
    visual::TypeNameWidget type_name("Test");
    type_name.setLocalPos(Pt(50, 100));
    
    EXPECT_EQ(type_name.worldPos().x, 50);
    EXPECT_EQ(type_name.worldPos().y, 100);
}

TEST(SwitchWidgetTest, InitialState) {
    visual::SwitchWidget sw(false, false);
    
    EXPECT_FALSE(sw.state());
    EXPECT_FALSE(sw.tripped());
}

TEST(SwitchWidgetTest, SetState) {
    visual::SwitchWidget sw;
    
    sw.setState(true);
    EXPECT_TRUE(sw.state());
    
    sw.setTripped(true);
    EXPECT_TRUE(sw.tripped());
}

TEST(SwitchWidgetTest, PreferredSize) {
    visual::SwitchWidget sw;
    Pt ps = sw.preferredSize(nullptr);
    
    EXPECT_EQ(ps.x, visual::SwitchWidget::MIN_WIDTH);
    EXPECT_EQ(ps.y, visual::SwitchWidget::HEIGHT);
}

TEST(SwitchWidgetTest, NotFlexible) {
    visual::SwitchWidget sw;
    EXPECT_FALSE(sw.isFlexible());
}

TEST(SwitchWidgetTest, UpdateFromContent) {
    visual::SwitchWidget sw;
    
    NodeContent content;
    content.state = true;
    content.tripped = true;
    
    sw.updateFromContent(content);
    
    EXPECT_TRUE(sw.state());
    EXPECT_TRUE(sw.tripped());
}

TEST(VerticalToggleTest, InitialState) {
    visual::VerticalToggleWidget toggle(false, false);
    
    EXPECT_FALSE(toggle.state());
    EXPECT_FALSE(toggle.tripped());
}

TEST(VerticalToggleTest, SetState) {
    visual::VerticalToggleWidget toggle;
    
    toggle.setState(true);
    EXPECT_TRUE(toggle.state());
    
    toggle.setTripped(true);
    EXPECT_TRUE(toggle.tripped());
}

TEST(VerticalToggleTest, PreferredSize) {
    visual::VerticalToggleWidget toggle;
    Pt ps = toggle.preferredSize(nullptr);
    
    EXPECT_EQ(ps.x, visual::VerticalToggleWidget::WIDTH);
    EXPECT_EQ(ps.y, visual::VerticalToggleWidget::HEIGHT);
}

TEST(VerticalToggleTest, NotFlexible) {
    visual::VerticalToggleWidget toggle;
    EXPECT_FALSE(toggle.isFlexible());
}

TEST(VerticalToggleTest, UpdateFromContent) {
    visual::VerticalToggleWidget toggle(false, false);
    
    NodeContent content;
    content.state = true;
    content.tripped = true;
    
    toggle.updateFromContent(content);
    
    EXPECT_TRUE(toggle.state());
    EXPECT_TRUE(toggle.tripped());
}

TEST(VoltmeterWidgetTest, InitialValue) {
    visual::VoltmeterWidget vm(12.5f, 0.0f, 30.0f, "V");
    
    EXPECT_FLOAT_EQ(vm.getValue(), 12.5f);
}

TEST(VoltmeterWidgetTest, SetValue) {
    visual::VoltmeterWidget vm;
    
    vm.setValue(24.0f);
    
    EXPECT_FLOAT_EQ(vm.getValue(), 24.0f);
}

TEST(VoltmeterWidgetTest, PreferredSize) {
    visual::VoltmeterWidget vm;
    
    Pt ps = vm.preferredSize(nullptr);
    
    EXPECT_EQ(ps.x, visual::VoltmeterWidget::GAUGE_RADIUS * 2.0f);
    EXPECT_GT(ps.y, visual::VoltmeterWidget::GAUGE_RADIUS * 2.0f);
}

TEST(VoltmeterWidgetTest, UpdateFromContent) {
    visual::VoltmeterWidget vm(0.0f, 0.0f, 30.0f, "V");
    
    NodeContent content;
    content.value = 15.5f;
    
    vm.updateFromContent(content);
    
    EXPECT_FLOAT_EQ(vm.getValue(), 15.5f);
}

TEST(VoltmeterWidgetTest, NotFlexible) {
    visual::VoltmeterWidget vm;
    EXPECT_FALSE(vm.isFlexible());
}

TEST(ContentWidgetTest, InLayout) {
    visual::Row row;
    auto* header = row.emplaceChild<visual::HeaderWidget>("Test", 0xFF404040);
    auto* sw = row.emplaceChild<visual::SwitchWidget>();
    
    row.setLocalPos(Pt(100, 200));
    row.layout(200, 24);
    
    EXPECT_EQ(header->worldPos().x, 100);
    EXPECT_EQ(header->worldPos().y, 200);
    EXPECT_EQ(sw->worldPos().y, 200);
}

TEST(ContentWidgetTest, NestedInContainer) {
    visual::Container container(Edges::all(5));
    container.emplaceChild<visual::VoltmeterWidget>();
    
    container.layout(100, 120);
    
    Pt child_pos = container.children()[0]->localPos();
    EXPECT_EQ(child_pos.x, 5);
    EXPECT_EQ(child_pos.y, 5);
}
