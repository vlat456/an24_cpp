#include "ui/math/pt.h"

using ui::Pt;

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

class MockDrawList : public IDrawList {
public:
    void add_line(Pt, Pt, uint32_t, float) override {}
    void add_rect(Pt, Pt, uint32_t, float) override {}
    void add_rect_with_rounding_corners(Pt, Pt, uint32_t, float, int, float) override {}
    void add_rect_filled(Pt, Pt, uint32_t) override {}
    void add_rect_filled_with_rounding(Pt, Pt, uint32_t, float) override {}
    void add_rect_filled_with_rounding_corners(Pt, Pt, uint32_t, float, int) override {}
    void add_circle(Pt, float, uint32_t, int) override {}
    void add_circle_filled(Pt, float, uint32_t, int) override {}
    void add_text(Pt, const char*, uint32_t, float) override {}
    void add_polyline(const Pt*, size_t, uint32_t, float) override {}
    void add_triangle_filled(Pt, Pt, Pt, uint32_t) override {}
    Pt calc_text_size(const char* text, float font_size) const override {
        return Pt(std::strlen(text) * font_size * 0.6f, font_size);
    }
};

/// Draw list that records all draw calls for verification in tests.
struct CircleCall {
    Pt center;
    float radius;
    uint32_t color;
    int segments;
    bool filled;
};

class RecordingDrawList : public IDrawList {
public:
    std::vector<CircleCall> circle_calls;
    int total_calls = 0;

    void add_line(Pt, Pt, uint32_t, float) override { ++total_calls; }
    void add_rect(Pt, Pt, uint32_t, float) override { ++total_calls; }
    void add_rect_with_rounding_corners(Pt, Pt, uint32_t, float, int, float) override { ++total_calls; }
    void add_rect_filled(Pt, Pt, uint32_t) override { ++total_calls; }
    void add_rect_filled_with_rounding(Pt, Pt, uint32_t, float) override { ++total_calls; }
    void add_rect_filled_with_rounding_corners(Pt, Pt, uint32_t, float, int) override { ++total_calls; }
    void add_circle(Pt c, float r, uint32_t color, int seg) override {
        ++total_calls;
        circle_calls.push_back({c, r, color, seg, false});
    }
    void add_circle_filled(Pt c, float r, uint32_t color, int seg) override {
        ++total_calls;
        circle_calls.push_back({c, r, color, seg, true});
    }
    void add_text(Pt, const char*, uint32_t, float) override { ++total_calls; }
    void add_polyline(const Pt*, size_t, uint32_t, float) override { ++total_calls; }
    void add_triangle_filled(Pt, Pt, Pt, uint32_t) override { ++total_calls; }
    Pt calc_text_size(const char* text, float font_size) const override {
        return Pt(std::strlen(text) * font_size * 0.6f, font_size);
    }
};

} // namespace visual

/// Helper: create a default RenderContext at zoom=1 with origin at (0,0).
static visual::RenderContext make_default_render_ctx() {
    visual::RenderContext ctx;
    ctx.zoom = 1.0f;
    ctx.pan = Pt(0, 0);
    ctx.canvas_min = Pt(0, 0);
    return ctx;
}

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
    visual::Container container(ui::Edges::all(5));
    container.emplaceChild<visual::VoltmeterWidget>();
    
    container.layout(100, 120);
    
    Pt child_pos = container.children()[0]->localPos();
    EXPECT_EQ(child_pos.x, 5);
    EXPECT_EQ(child_pos.y, 5);
}

TEST(IndicatorWidgetTest, InitialBrightness) {
    visual::IndicatorWidget ind;
    
    EXPECT_FLOAT_EQ(ind.getBrightness(), 0.0f);
}

TEST(IndicatorWidgetTest, SetBrightness) {
    visual::IndicatorWidget ind;
    
    ind.setBrightness(0.75f);
    EXPECT_FLOAT_EQ(ind.getBrightness(), 0.75f);
}

TEST(IndicatorWidgetTest, PreferredSize) {
    visual::IndicatorWidget ind;
    visual::MockDrawList dl;
    
    Pt ps = ind.preferredSize(&dl);
    
    EXPECT_EQ(ps.x, visual::IndicatorWidget::SIZE);
    EXPECT_EQ(ps.y, visual::IndicatorWidget::SIZE);
}

TEST(IndicatorWidgetTest, NotFlexible) {
    visual::IndicatorWidget ind;
    EXPECT_FALSE(ind.isFlexible());
}

TEST(IndicatorWidgetTest, UpdateFromContent) {
    visual::IndicatorWidget ind;
    
    NodeContent content;
    content.value = 0.6f;
    
    ind.updateFromContent(content);
    
    EXPECT_FLOAT_EQ(ind.getBrightness(), 0.6f);
}

TEST(IndicatorWidgetTest, UpdateFromContentClampsToOne) {
    visual::IndicatorWidget ind;
    
    NodeContent content;
    content.value = 1.5f;  // over 1.0
    
    ind.updateFromContent(content);
    
    EXPECT_FLOAT_EQ(ind.getBrightness(), 1.0f);
}

TEST(IndicatorWidgetTest, UpdateFromContentClampsToZero) {
    visual::IndicatorWidget ind;
    
    NodeContent content;
    content.value = -0.5f;  // negative
    
    ind.updateFromContent(content);
    
    EXPECT_FLOAT_EQ(ind.getBrightness(), 0.0f);
}

// ============================================================================
// IndicatorWidget rendering tests
// ============================================================================

TEST(IndicatorWidgetTest, RenderEmitsFilledCircleAndOutline) {
    // Verify that render() produces exactly 2 circle calls:
    // one filled circle (the lamp body) and one outline circle (the border).
    visual::IndicatorWidget ind(0.5f);
    ind.setLocalPos(Pt(0, 0));
    ind.layout(visual::IndicatorWidget::SIZE, visual::IndicatorWidget::SIZE);

    visual::RecordingDrawList dl;
    auto ctx = make_default_render_ctx();

    ind.render(&dl, ctx);

    // Must have exactly 2 circle calls: filled + outline
    ASSERT_EQ(dl.circle_calls.size(), 2u);
    EXPECT_TRUE(dl.circle_calls[0].filled) << "First circle should be filled (lamp body)";
    EXPECT_FALSE(dl.circle_calls[1].filled) << "Second circle should be outline (border)";
}

TEST(IndicatorWidgetTest, RenderCircleCenteredInWidget) {
    visual::IndicatorWidget ind(0.0f);
    ind.setLocalPos(Pt(10, 20));
    ind.layout(visual::IndicatorWidget::SIZE, visual::IndicatorWidget::SIZE);

    visual::RecordingDrawList dl;
    auto ctx = make_default_render_ctx();
    ind.render(&dl, ctx);

    ASSERT_GE(dl.circle_calls.size(), 1u);
    // Center should be at (10 + SIZE/2, 20 + SIZE/2) with zoom=1
    float expected_cx = 10.0f + visual::IndicatorWidget::SIZE * 0.5f;
    float expected_cy = 20.0f + visual::IndicatorWidget::SIZE * 0.5f;
    EXPECT_NEAR(dl.circle_calls[0].center.x, expected_cx, 0.01f);
    EXPECT_NEAR(dl.circle_calls[0].center.y, expected_cy, 0.01f);
}

TEST(IndicatorWidgetTest, RenderRadiusGrowsWithBrightness) {
    // At brightness=0, radius should be smaller than at brightness=1
    auto render_and_get_radius = [](float brightness) -> float {
        visual::IndicatorWidget ind(brightness);
        ind.setLocalPos(Pt(0, 0));
        ind.layout(visual::IndicatorWidget::SIZE, visual::IndicatorWidget::SIZE);

        visual::RecordingDrawList dl;
        auto ctx = make_default_render_ctx();
        ind.render(&dl, ctx);
        return dl.circle_calls.empty() ? 0.0f : dl.circle_calls[0].radius;
    };

    float r_off = render_and_get_radius(0.0f);
    float r_half = render_and_get_radius(0.5f);
    float r_full = render_and_get_radius(1.0f);

    EXPECT_GT(r_off, 0.0f) << "Radius should be positive even when off";
    EXPECT_GT(r_half, r_off) << "Radius should grow with brightness";
    EXPECT_GT(r_full, r_half) << "Radius should grow with brightness";
}

TEST(IndicatorWidgetTest, RenderColorOffIsGray) {
    visual::IndicatorWidget ind(0.0f);
    ind.setLocalPos(Pt(0, 0));
    ind.layout(visual::IndicatorWidget::SIZE, visual::IndicatorWidget::SIZE);

    visual::RecordingDrawList dl;
    auto ctx = make_default_render_ctx();
    ind.render(&dl, ctx);

    ASSERT_GE(dl.circle_calls.size(), 1u);
    // When brightness = 0, fill_color should be COLOR_OFF (0xFF505050)
    EXPECT_EQ(dl.circle_calls[0].color, 0xFF505050u)
        << "Off state should use gray color (COLOR_OFF)";
}

TEST(IndicatorWidgetTest, RenderColorOnHasGreenChannel) {
    visual::IndicatorWidget ind(1.0f);
    ind.setLocalPos(Pt(0, 0));
    ind.layout(visual::IndicatorWidget::SIZE, visual::IndicatorWidget::SIZE);

    visual::RecordingDrawList dl;
    auto ctx = make_default_render_ctx();
    ind.render(&dl, ctx);

    ASSERT_GE(dl.circle_calls.size(), 1u);
    uint32_t color = dl.circle_calls[0].color;
    // ImGui color format is AABBGGRR
    uint8_t g = (color >> 8) & 0xFF;
    EXPECT_GT(g, 200u) << "Full brightness should have dominant green channel";
}

TEST(IndicatorWidgetTest, RenderAtZoom2ScalesRadius) {
    visual::IndicatorWidget ind(0.5f);
    ind.setLocalPos(Pt(0, 0));
    ind.layout(visual::IndicatorWidget::SIZE, visual::IndicatorWidget::SIZE);

    // Render at zoom=1
    visual::RecordingDrawList dl1;
    auto ctx1 = make_default_render_ctx();
    ctx1.zoom = 1.0f;
    ind.render(&dl1, ctx1);

    // Render at zoom=2
    visual::RecordingDrawList dl2;
    auto ctx2 = make_default_render_ctx();
    ctx2.zoom = 2.0f;
    ind.render(&dl2, ctx2);

    ASSERT_GE(dl1.circle_calls.size(), 1u);
    ASSERT_GE(dl2.circle_calls.size(), 1u);
    EXPECT_NEAR(dl2.circle_calls[0].radius, dl1.circle_calls[0].radius * 2.0f, 0.01f)
        << "Circle radius should scale linearly with zoom";
}

TEST(IndicatorWidgetTest, RenderBorderAlwaysPresent) {
    // Verify the outline circle is drawn regardless of brightness
    for (float b : {0.0f, 0.5f, 1.0f}) {
        visual::IndicatorWidget ind(b);
        ind.setLocalPos(Pt(0, 0));
        ind.layout(visual::IndicatorWidget::SIZE, visual::IndicatorWidget::SIZE);

        visual::RecordingDrawList dl;
        auto ctx = make_default_render_ctx();
        ind.render(&dl, ctx);

        ASSERT_EQ(dl.circle_calls.size(), 2u)
            << "Should always draw filled circle + outline, brightness=" << b;
        // Outline color should be 0xFF404040
        EXPECT_EQ(dl.circle_calls[1].color, 0xFF404040u)
            << "Border color should be 0xFF404040, brightness=" << b;
    }
}

TEST(IndicatorWidgetTest, LayoutAcceptsParentSize) {
    // Verify layout() preserves parent-assigned size via Widget::layout()
    visual::IndicatorWidget ind;
    ind.layout(100.0f, 100.0f);
    // Widget::layout base call sets size
    EXPECT_EQ(ind.size().x, 100.0f);
    EXPECT_EQ(ind.size().y, 100.0f);
}

TEST(IndicatorWidgetTest, RenderCircleCenteredInLargerWidget) {
    // Regression: When Container gives the IndicatorWidget a size larger than
    // its natural SIZE (e.g., full node width), the circle must be centered
    // in the actual widget bounds, not at the hardcoded SIZE offset.
    visual::IndicatorWidget ind(0.5f);
    float large_w = 80.0f;  // Much wider than SIZE=24
    float large_h = 60.0f;  // Taller than SIZE=24
    ind.setLocalPos(Pt(10, 20));
    ind.layout(large_w, large_h);

    visual::RecordingDrawList dl;
    auto ctx = make_default_render_ctx();
    ind.render(&dl, ctx);

    ASSERT_GE(dl.circle_calls.size(), 1u);
    // Center should be at (10 + 80/2, 20 + 60/2) = (50, 50), not (22, 32)
    float expected_cx = 10.0f + large_w * 0.5f;
    float expected_cy = 20.0f + large_h * 0.5f;
    EXPECT_NEAR(dl.circle_calls[0].center.x, expected_cx, 0.01f)
        << "Circle X must be centered in actual widget width, not hardcoded SIZE";
    EXPECT_NEAR(dl.circle_calls[0].center.y, expected_cy, 0.01f)
        << "Circle Y must be centered in actual widget height, not hardcoded SIZE";
}

TEST(IndicatorWidgetTest, RenderInContainerEmitsDrawCalls) {
    // Verify the widget renders correctly when nested in a Container,
    // simulating real usage in the node layout tree.
    visual::Container container(ui::Edges::all(4));
    auto* ind = container.emplaceChild<visual::IndicatorWidget>(0.8f);

    container.setLocalPos(Pt(50, 60));
    container.layout(40, 40);

    visual::RecordingDrawList dl;
    auto ctx = make_default_render_ctx();
    // renderTree renders the container and all children
    container.renderTree(&dl, ctx);

    // The indicator should have emitted its 2 circle calls
    EXPECT_GE(dl.circle_calls.size(), 2u)
        << "IndicatorWidget in Container must render circle calls via renderTree";
    // Verify the filled circle has a non-zero radius
    EXPECT_GT(dl.circle_calls[0].radius, 0.0f);
    EXPECT_TRUE(dl.circle_calls[0].filled);
}

// ============================================================================
// interaction_target() tests — content widgets publish their interaction role
// ============================================================================

TEST(SwitchWidgetTest, InteractionTargetIsToggle) {
    visual::SwitchWidget sw;
    auto target = sw.interaction_target(Pt(sw.size().x * 0.5f, sw.size().y * 0.5f));
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->role, visual::InteractionRole::Toggle);
}

TEST(SwitchWidgetTest, InteractionTargetCoversWidgetEdges) {
    visual::SwitchWidget sw;
    auto affordance = sw.affordance_bounds_local();
    EXPECT_EQ(affordance.size.x, sw.size().x);
    EXPECT_EQ(affordance.size.y, sw.size().y);
    auto top_left = sw.interaction_target(Pt(1.0f, 1.0f));
    auto bottom_right = sw.interaction_target(Pt(sw.size().x - 1.0f, sw.size().y - 1.0f));
    ASSERT_TRUE(top_left.has_value());
    ASSERT_TRUE(bottom_right.has_value());
    EXPECT_EQ(top_left->role, visual::InteractionRole::Toggle);
    EXPECT_EQ(bottom_right->role, visual::InteractionRole::Toggle);
}

TEST(VerticalToggleTest, InteractionTargetIsToggle) {
    visual::VerticalToggleWidget toggle;
    auto target = toggle.interaction_target(Pt(toggle.size().x * 0.5f, toggle.size().y * 0.5f));
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->role, visual::InteractionRole::Toggle);
}

TEST(VerticalToggleTest, InteractionTargetCoversWidgetEdges) {
    visual::VerticalToggleWidget toggle;
    auto affordance = toggle.affordance_bounds_local();
    EXPECT_EQ(affordance.size.x, visual::VerticalToggleWidget::WIDTH);
    EXPECT_EQ(affordance.size.y, visual::VerticalToggleWidget::HEIGHT);
    auto top_edge = toggle.interaction_target(Pt(toggle.size().x * 0.5f, 1.0f));
    auto bottom_edge = toggle.interaction_target(Pt(toggle.size().x * 0.5f, toggle.size().y - 1.0f));
    ASSERT_TRUE(top_edge.has_value());
    ASSERT_TRUE(bottom_edge.has_value());
    EXPECT_EQ(top_edge->role, visual::InteractionRole::Toggle);
    EXPECT_EQ(bottom_edge->role, visual::InteractionRole::Toggle);
}

TEST(SliderWidgetTest, InteractionTargetIsContinuousScalar) {
    visual::SliderWidget slider;
    auto target = slider.interaction_target(Pt(slider.size().x * 0.5f, slider.size().y * 0.5f));
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->role, visual::InteractionRole::ContinuousScalar);
}

TEST(SliderWidgetTest, InteractionTargetCoversWidgetEdges) {
    visual::SliderWidget slider;
    auto left = slider.interaction_target(Pt(1.0f, slider.size().y * 0.5f));
    auto right = slider.interaction_target(Pt(slider.size().x - 1.0f, slider.size().y * 0.5f));
    ASSERT_TRUE(left.has_value());
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(left->role, visual::InteractionRole::ContinuousScalar);
    EXPECT_EQ(right->role, visual::InteractionRole::ContinuousScalar);
}

TEST(VoltmeterWidgetTest, NoInteractionTarget) {
    visual::VoltmeterWidget vm;
    auto target = vm.interaction_target(Pt(vm.size().x * 0.5f, vm.size().y * 0.5f));
    EXPECT_FALSE(target.has_value());
}

TEST(IndicatorWidgetTest, NoInteractionTarget) {
    visual::IndicatorWidget ind;
    auto target = ind.interaction_target(Pt(ind.size().x * 0.5f, ind.size().y * 0.5f));
    EXPECT_FALSE(target.has_value());
}

TEST(KnobWidgetTest, InteractionTargetCoversVisibleRingNearEdge) {
    visual::KnobWidget knob(0, 5);
    auto affordance = knob.affordance_bounds_local();
    EXPECT_EQ(affordance.size.x, visual::KnobWidget::SIZE);
    EXPECT_EQ(affordance.size.y, visual::KnobWidget::SIZE);
    const float cx = knob.size().x * 0.5f;
    const float cy = knob.size().y * 0.5f;

    auto edge = knob.interaction_target(Pt(cx + visual::KnobWidget::TICK_OUTER - 1.0f, cy));
    ASSERT_TRUE(edge.has_value());
    EXPECT_EQ(edge->role, visual::InteractionRole::DiscreteSelector);
}

TEST(KnobWidgetTest, InteractionTargetCoversInteriorCorner) {
    visual::KnobWidget knob(0, 5);
    auto corner = knob.interaction_target(Pt(2.0f, 2.0f));
    ASSERT_TRUE(corner.has_value());
    EXPECT_EQ(corner->role, visual::InteractionRole::DiscreteSelector);
}

TEST(HeaderWidgetTest, NoInteractionTarget) {
    visual::HeaderWidget header("Test", 0xFF404040);
    auto target = header.interaction_target(Pt(5.0f, 5.0f));
    EXPECT_FALSE(target.has_value());
}

TEST(TypeNameWidgetTest, NoInteractionTarget) {
    visual::TypeNameWidget type_name("Test");
    auto target = type_name.interaction_target(Pt(5.0f, 5.0f));
    EXPECT_FALSE(target.has_value());
}
