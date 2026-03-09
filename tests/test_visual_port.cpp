#include <gtest/gtest.h>
#include "editor/visual/port/port.h"
#include "editor/visual/renderer/render_theme.h"
#include "editor/visual/renderer/draw_list.h"

// ============================================================================
// Rich mock draw list that captures geometry details
// ============================================================================

struct PortTestDrawList : IDrawList {
    struct Circle { Pt center; float radius; uint32_t color; };
    struct Text { Pt pos; std::string text; uint32_t color; float font_size; };

    std::vector<Circle> circles;
    std::vector<Text> texts;

    void add_line(Pt, Pt, uint32_t, float) override {}
    void add_rect(Pt, Pt, uint32_t, float) override {}
    void add_rect_with_rounding_corners(Pt, Pt, uint32_t, float, int, float = 1.0f) override {}
    void add_rect_filled(Pt, Pt, uint32_t) override {}
    void add_circle(Pt, float, uint32_t, int) override {}
    void add_circle_filled(Pt center, float radius, uint32_t color, int) override {
        circles.push_back({center, radius, color});
    }
    void add_text(Pt pos, const char* text, uint32_t color, float font_size) override {
        texts.push_back({pos, text, color, font_size});
    }
    void add_polyline(const Pt*, size_t, uint32_t, float) override {}
    void add_rect_filled_with_rounding(Pt, Pt, uint32_t, float) override {}
    void add_rect_filled_with_rounding_corners(Pt, Pt, uint32_t, float, int) override {}
    Pt calc_text_size(const char* text, float font_size) const override {
        return Pt(strlen(text) * font_size * 0.6f, font_size);
    }
};

// ============================================================================
// Construction
// ============================================================================

TEST(VisualPortTest, Construction_DefaultType) {
    VisualPort port("v_in", PortSide::Input);
    EXPECT_EQ(port.name(), "v_in");
    EXPECT_EQ(port.side(), PortSide::Input);
    EXPECT_EQ(port.type(), an24::PortType::Any);
    EXPECT_FALSE(port.isAlias());
    EXPECT_EQ(port.logicalName(), "v_in");
}

TEST(VisualPortTest, Construction_WithType) {
    VisualPort port("v_out", PortSide::Output, an24::PortType::V);
    EXPECT_EQ(port.type(), an24::PortType::V);
    EXPECT_EQ(port.side(), PortSide::Output);
}

TEST(VisualPortTest, Construction_BusAlias) {
    VisualPort port("wire_3", PortSide::InOut, an24::PortType::V, "v");
    EXPECT_TRUE(port.isAlias());
    EXPECT_EQ(port.name(), "wire_3");
    EXPECT_EQ(port.targetPort(), "v");
    EXPECT_EQ(port.logicalName(), "v");
}

// ============================================================================
// Color
// ============================================================================

TEST(VisualPortTest, Color_MatchesPortType) {
    VisualPort v("p", PortSide::Input, an24::PortType::V);
    EXPECT_EQ(v.color(), render_theme::get_port_color(an24::PortType::V));

    VisualPort i("p", PortSide::Input, an24::PortType::I);
    EXPECT_EQ(i.color(), render_theme::get_port_color(an24::PortType::I));

    VisualPort b("p", PortSide::Input, an24::PortType::Bool);
    EXPECT_EQ(b.color(), render_theme::get_port_color(an24::PortType::Bool));

    VisualPort any("p", PortSide::Input, an24::PortType::Any);
    EXPECT_EQ(any.color(), render_theme::get_port_color(an24::PortType::Any));
}

// ============================================================================
// Type Compatibility (static)
// ============================================================================

TEST(VisualPortTest, TypeCompat_AnyIsWildcard) {
    EXPECT_TRUE(VisualPort::areTypesCompatible(an24::PortType::Any, an24::PortType::V));
    EXPECT_TRUE(VisualPort::areTypesCompatible(an24::PortType::V, an24::PortType::Any));
    EXPECT_TRUE(VisualPort::areTypesCompatible(an24::PortType::Any, an24::PortType::Any));
}

TEST(VisualPortTest, TypeCompat_SameTypeMatch) {
    EXPECT_TRUE(VisualPort::areTypesCompatible(an24::PortType::V, an24::PortType::V));
    EXPECT_TRUE(VisualPort::areTypesCompatible(an24::PortType::I, an24::PortType::I));
    EXPECT_TRUE(VisualPort::areTypesCompatible(an24::PortType::Bool, an24::PortType::Bool));
}

TEST(VisualPortTest, TypeCompat_DifferentTypeReject) {
    EXPECT_FALSE(VisualPort::areTypesCompatible(an24::PortType::V, an24::PortType::I));
    EXPECT_FALSE(VisualPort::areTypesCompatible(an24::PortType::Bool, an24::PortType::RPM));
}

// ============================================================================
// Side Compatibility (static)
// ============================================================================

TEST(VisualPortTest, SideCompat_InOutToAnything) {
    EXPECT_TRUE(VisualPort::areSidesCompatible(PortSide::InOut, PortSide::Input));
    EXPECT_TRUE(VisualPort::areSidesCompatible(PortSide::InOut, PortSide::Output));
    EXPECT_TRUE(VisualPort::areSidesCompatible(PortSide::InOut, PortSide::InOut));
    EXPECT_TRUE(VisualPort::areSidesCompatible(PortSide::Input, PortSide::InOut));
    EXPECT_TRUE(VisualPort::areSidesCompatible(PortSide::Output, PortSide::InOut));
}

TEST(VisualPortTest, SideCompat_InputToOutput) {
    EXPECT_TRUE(VisualPort::areSidesCompatible(PortSide::Input, PortSide::Output));
    EXPECT_TRUE(VisualPort::areSidesCompatible(PortSide::Output, PortSide::Input));
}

TEST(VisualPortTest, SideCompat_SameSideReject) {
    EXPECT_FALSE(VisualPort::areSidesCompatible(PortSide::Input, PortSide::Input));
    EXPECT_FALSE(VisualPort::areSidesCompatible(PortSide::Output, PortSide::Output));
}

// ============================================================================
// Full Compatibility (isCompatibleWith)
// ============================================================================

TEST(VisualPortTest, Compatible_VoltageInToVoltageOut) {
    VisualPort a("a", PortSide::Output, an24::PortType::V);
    VisualPort b("b", PortSide::Input, an24::PortType::V);
    EXPECT_TRUE(a.isCompatibleWith(b));
    EXPECT_TRUE(b.isCompatibleWith(a));
}

TEST(VisualPortTest, Incompatible_VoltageToCurrentReject) {
    VisualPort a("a", PortSide::Output, an24::PortType::V);
    VisualPort b("b", PortSide::Input, an24::PortType::I);
    EXPECT_FALSE(a.isCompatibleWith(b));
}

TEST(VisualPortTest, Incompatible_SameSideReject) {
    VisualPort a("a", PortSide::Output, an24::PortType::V);
    VisualPort b("b", PortSide::Output, an24::PortType::V);
    EXPECT_FALSE(a.isCompatibleWith(b));
}

TEST(VisualPortTest, Compatible_InOutBusToOutput) {
    VisualPort bus("v", PortSide::InOut, an24::PortType::V);
    VisualPort out("v_out", PortSide::Output, an24::PortType::V);
    EXPECT_TRUE(bus.isCompatibleWith(out));
}

TEST(VisualPortTest, Compatible_AnyTypeAcceptsAll) {
    VisualPort any("x", PortSide::Input, an24::PortType::Any);
    VisualPort v("y", PortSide::Output, an24::PortType::V);
    EXPECT_TRUE(any.isCompatibleWith(v));
}

// ============================================================================
// calcBounds
// ============================================================================

TEST(VisualPortTest, CalcBounds_CenteredOnWorldPos) {
    VisualPort port("v", PortSide::Input, an24::PortType::V);
    port.setWorldPosition(Pt(100.0f, 200.0f));

    Bounds b = port.calcBounds();
    EXPECT_FLOAT_EQ(b.x, 100.0f - VisualPort::RADIUS);
    EXPECT_FLOAT_EQ(b.y, 200.0f - VisualPort::RADIUS);
    EXPECT_FLOAT_EQ(b.w, VisualPort::RADIUS * 2);
    EXPECT_FLOAT_EQ(b.h, VisualPort::RADIUS * 2);
}

TEST(VisualPortTest, CalcBounds_ContainsCenter) {
    VisualPort port("v", PortSide::Input);
    port.setWorldPosition(Pt(50.0f, 75.0f));
    Bounds b = port.calcBounds();
    EXPECT_TRUE(b.contains(50.0f, 75.0f));
}

// ============================================================================
// World position
// ============================================================================

TEST(VisualPortTest, WorldPosition_SetGet) {
    VisualPort port("p", PortSide::Input);
    port.setWorldPosition(Pt(42.0f, 99.0f));
    EXPECT_FLOAT_EQ(port.worldPosition().x, 42.0f);
    EXPECT_FLOAT_EQ(port.worldPosition().y, 99.0f);
}

// ============================================================================
// Render (self-drawing)
// ============================================================================

TEST(VisualPortTest, Render_DrawsCircle) {
    VisualPort port("v_in", PortSide::Input, an24::PortType::V);

    PortTestDrawList dl;
    Pt origin(100.0f, 200.0f);
    float zoom = 1.0f;

    port.render(&dl, origin, zoom);

    ASSERT_EQ(dl.circles.size(), 1u);
    EXPECT_FLOAT_EQ(dl.circles[0].center.x, 100.0f);
    EXPECT_FLOAT_EQ(dl.circles[0].center.y, 200.0f);
    EXPECT_FLOAT_EQ(dl.circles[0].radius, VisualPort::RADIUS);
    EXPECT_EQ(dl.circles[0].color, render_theme::get_port_color(an24::PortType::V));
}

TEST(VisualPortTest, Render_WithLabelRight) {
    VisualPort port("v_in", PortSide::Input, an24::PortType::V);
    port.setLabel("v_in");
    port.setLabelSide(VisualPort::LabelSide::Right);

    PortTestDrawList dl;
    port.render(&dl, Pt(50, 50), 1.0f);

    EXPECT_EQ(dl.circles.size(), 1u);
    EXPECT_EQ(dl.texts.size(), 1u);
    EXPECT_EQ(dl.texts[0].text, "v_in");
    // Label should be to the right of the circle
    EXPECT_GT(dl.texts[0].pos.x, 50.0f);
}

TEST(VisualPortTest, Render_WithLabelLeft) {
    VisualPort port("v_out", PortSide::Output, an24::PortType::V);
    port.setLabel("v_out");
    port.setLabelSide(VisualPort::LabelSide::Left);

    PortTestDrawList dl;
    port.render(&dl, Pt(100, 50), 1.0f);

    EXPECT_EQ(dl.texts.size(), 1u);
    // Label should be to the left of the circle
    EXPECT_LT(dl.texts[0].pos.x, 100.0f);
}

TEST(VisualPortTest, Render_NoLabelByDefault) {
    VisualPort port("v_in", PortSide::Input, an24::PortType::V);
    // Default label_side_ is None — should not draw text

    PortTestDrawList dl;
    port.render(&dl, Pt(50, 50), 1.0f);

    EXPECT_EQ(dl.circles.size(), 1u);
    EXPECT_EQ(dl.texts.size(), 0u);
}

TEST(VisualPortTest, Render_ZoomScales) {
    VisualPort port("p", PortSide::Input, an24::PortType::V);

    PortTestDrawList dl;
    port.render(&dl, Pt(0, 0), 2.0f);

    ASSERT_EQ(dl.circles.size(), 1u);
    EXPECT_FLOAT_EQ(dl.circles[0].radius, VisualPort::RADIUS * 2.0f);
}

// ============================================================================
// Regression: Bus alias ports
// ============================================================================

TEST(VisualPortTest, BusAlias_LogicalNameIsTargetPort) {
    VisualPort alias("wire_5", PortSide::InOut, an24::PortType::V, "v");
    EXPECT_EQ(alias.logicalName(), "v");
    EXPECT_EQ(alias.name(), "wire_5");
    EXPECT_TRUE(alias.isAlias());
}

TEST(VisualPortTest, BusAlias_CompatibleWithOutputV) {
    VisualPort bus_alias("wire_1", PortSide::InOut, an24::PortType::V, "v");
    VisualPort battery_out("v_out", PortSide::Output, an24::PortType::V);
    EXPECT_TRUE(bus_alias.isCompatibleWith(battery_out));
}

// ============================================================================
// Regression: setType updates color
// ============================================================================

TEST(VisualPortTest, SetType_UpdatesColor) {
    VisualPort port("p", PortSide::Input, an24::PortType::Any);
    EXPECT_EQ(port.color(), render_theme::get_port_color(an24::PortType::Any));

    port.setType(an24::PortType::V);
    EXPECT_EQ(port.color(), render_theme::get_port_color(an24::PortType::V));
}
