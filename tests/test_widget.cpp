#include <gtest/gtest.h>
#include "editor/widget.h"
#include "editor/visual_node.h"
#include "editor/render.h"
#include "editor/data/node.h"

// ============================================================================
// Detailed MockDrawList for recording draw calls
// ============================================================================

class DetailedMockDrawList : public IDrawList {
public:
    struct CircleCall {
        Pt center;
        float radius;
        uint32_t color;
    };
    std::vector<CircleCall> circles;
    bool had_rect_ = false;

    void add_line(Pt, Pt, uint32_t, float) override {}
    void add_rect(Pt, Pt, uint32_t, float) override { had_rect_ = true; }
    void add_rect_filled(Pt, Pt, uint32_t) override { had_rect_ = true; }
    void add_circle(Pt, float, uint32_t, int) override {}
    void add_circle_filled(Pt center, float radius, uint32_t color, int) override {
        circles.push_back({center, radius, color});
    }
    void add_text(Pt, const char*, uint32_t, float) override {}
    Pt calc_text_size(const char* text, float font_size) const override {
        return Pt(strlen(text) * font_size * 0.6f, font_size);
    }
    void add_polyline(const Pt*, size_t, uint32_t, float) override {}
};

// ============================================================================
// Widget base tests
// ============================================================================

TEST(WidgetTest, DefaultState) {
    // Can't instantiate Widget directly (abstract), but test via ColumnLayout
    ColumnLayout layout;
    EXPECT_FLOAT_EQ(layout.x(), 0.0f);
    EXPECT_FLOAT_EQ(layout.y(), 0.0f);
    EXPECT_FLOAT_EQ(layout.width(), 0.0f);
    EXPECT_FLOAT_EQ(layout.height(), 0.0f);
    EXPECT_FALSE(layout.isFlexible());
}

TEST(WidgetTest, SetPosition) {
    ColumnLayout layout;
    layout.setPosition(10.0f, 20.0f);
    EXPECT_FLOAT_EQ(layout.x(), 10.0f);
    EXPECT_FLOAT_EQ(layout.y(), 20.0f);
}

TEST(WidgetTest, SetSize) {
    ColumnLayout layout;
    layout.setSize(100.0f, 50.0f);
    Pt sz = layout.getSize();
    EXPECT_FLOAT_EQ(sz.x, 100.0f);
    EXPECT_FLOAT_EQ(sz.y, 50.0f);
}

TEST(WidgetTest, GetBounds) {
    ColumnLayout layout;
    layout.setPosition(5.0f, 10.0f);
    layout.setSize(100.0f, 50.0f);
    Bounds b = layout.getBounds();
    EXPECT_FLOAT_EQ(b.x, 5.0f);
    EXPECT_FLOAT_EQ(b.y, 10.0f);
    EXPECT_FLOAT_EQ(b.w, 100.0f);
    EXPECT_FLOAT_EQ(b.h, 50.0f);
    EXPECT_TRUE(b.contains(50.0f, 30.0f));
    EXPECT_FALSE(b.contains(0.0f, 0.0f));
}

// ============================================================================
// ColumnLayout tests
// ============================================================================

TEST(ColumnLayoutTest, Empty) {
    ColumnLayout layout;
    EXPECT_EQ(layout.childCount(), 0u);
    layout.layout(100.0f, 80.0f);
    EXPECT_FLOAT_EQ(layout.width(), 100.0f);
    EXPECT_FLOAT_EQ(layout.height(), 80.0f);
}

TEST(ColumnLayoutTest, AddWidget) {
    ColumnLayout layout;
    auto header = std::make_unique<HeaderWidget>("Test", 0xFF404060);
    Widget* ptr = layout.addWidget(std::move(header));
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(layout.childCount(), 1u);
    EXPECT_EQ(layout.child(0), ptr);
}

TEST(ColumnLayoutTest, FixedChildrenStackVertically) {
    ColumnLayout layout;
    layout.addWidget(std::make_unique<HeaderWidget>("Test", 0xFF404060));
    layout.addWidget(std::make_unique<PortRowWidget>("in", "out"));
    layout.addWidget(std::make_unique<TypeNameWidget>("Battery"));

    layout.layout(120.0f, 80.0f);

    // Header at y=0
    EXPECT_FLOAT_EQ(layout.child(0)->y(), 0.0f);
    EXPECT_FLOAT_EQ(layout.child(0)->height(), HeaderWidget::HEIGHT);

    // Port row after header
    EXPECT_FLOAT_EQ(layout.child(1)->y(), HeaderWidget::HEIGHT);
    EXPECT_FLOAT_EQ(layout.child(1)->height(), PortRowWidget::ROW_HEIGHT);

    // Type name after port row
    float expected_y = HeaderWidget::HEIGHT + PortRowWidget::ROW_HEIGHT;
    EXPECT_FLOAT_EQ(layout.child(2)->y(), expected_y);
}

TEST(ColumnLayoutTest, FlexibleChildTakesRemainingSpace) {
    ColumnLayout layout;
    layout.addWidget(std::make_unique<HeaderWidget>("Test", 0xFF404060));  // 24
    auto content = std::make_unique<ContentWidget>("label");              // flex
    layout.addWidget(std::move(content));
    layout.addWidget(std::make_unique<TypeNameWidget>("Type"));            // 16

    layout.layout(120.0f, 80.0f);

    // Flex child gets remaining space: 80 - 24 - 16 = 40
    EXPECT_FLOAT_EQ(layout.child(1)->height(), 40.0f);
    EXPECT_FLOAT_EQ(layout.child(1)->y(), 24.0f);
    EXPECT_FLOAT_EQ(layout.child(2)->y(), 64.0f);
}

TEST(ColumnLayoutTest, MultipleFlexChildrenShareSpace) {
    ColumnLayout layout;
    layout.addWidget(std::make_unique<HeaderWidget>("Test", 0xFF404060));  // 24

    auto c1 = std::make_unique<ContentWidget>("a");
    layout.addWidget(std::move(c1));  // flex
    auto c2 = std::make_unique<ContentWidget>("b");
    layout.addWidget(std::move(c2));  // flex

    layout.layout(120.0f, 80.0f);

    // Each flex gets (80 - 24) / 2 = 28
    EXPECT_FLOAT_EQ(layout.child(1)->height(), 28.0f);
    EXPECT_FLOAT_EQ(layout.child(2)->height(), 28.0f);
}

TEST(ColumnLayoutTest, AllChildrenGetFullWidth) {
    ColumnLayout layout;
    layout.addWidget(std::make_unique<HeaderWidget>("Test", 0xFF404060));
    layout.addWidget(std::make_unique<PortRowWidget>("in", "out"));
    layout.addWidget(std::make_unique<TypeNameWidget>("Type"));

    layout.layout(150.0f, 80.0f);

    for (size_t i = 0; i < layout.childCount(); i++) {
        EXPECT_FLOAT_EQ(layout.child(i)->width(), 150.0f);
    }
}

TEST(ColumnLayoutTest, PreferredSizeSumsHeights) {
    ColumnLayout layout;
    layout.addWidget(std::make_unique<HeaderWidget>("Test", 0xFF404060));  // 24
    layout.addWidget(std::make_unique<PortRowWidget>("in", "out"));        // 16
    layout.addWidget(std::make_unique<TypeNameWidget>("Type"));            // 16

    Pt pref = layout.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(pref.y, 24.0f + 16.0f + 16.0f);
}

// ============================================================================
// HeaderWidget tests
// ============================================================================

TEST(HeaderWidgetTest, PreferredSize) {
    HeaderWidget header("Battery", 0xFF404060);
    Pt pref = header.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(pref.y, HeaderWidget::HEIGHT);
}

TEST(HeaderWidgetTest, RenderDoesNotCrash) {
    HeaderWidget header("Test", 0xFF404060);
    header.layout(120.0f, HeaderWidget::HEIGHT);

    MockDrawList dl;
    header.render(&dl, Pt(0, 0), 1.0f);
    // Should have drawn text and rect
}

// ============================================================================
// TypeNameWidget tests
// ============================================================================

TEST(TypeNameWidgetTest, PreferredSize) {
    TypeNameWidget tn("Battery");
    Pt pref = tn.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(pref.y, TypeNameWidget::HEIGHT);
}

TEST(TypeNameWidgetTest, RenderCenteredText) {
    TypeNameWidget tn("Battery");
    tn.layout(120.0f, TypeNameWidget::HEIGHT);

    MockDrawList dl;
    tn.render(&dl, Pt(0, 0), 1.0f);
    // Should render centered type name text
}

// ============================================================================
// PortRowWidget tests
// ============================================================================

TEST(PortRowWidgetTest, PreferredSize) {
    PortRowWidget row("v_in", "v_out");
    Pt pref = row.getPreferredSize(nullptr);
    EXPECT_FLOAT_EQ(pref.y, PortRowWidget::ROW_HEIGHT);
}

TEST(PortRowWidgetTest, PortNames) {
    PortRowWidget row("input", "output");
    EXPECT_EQ(row.leftPortName(), "input");
    EXPECT_EQ(row.rightPortName(), "output");
}

TEST(PortRowWidgetTest, LeftPortCenterAtLeftEdge) {
    PortRowWidget row("in", "out");
    row.setPosition(0, 24.0f);  // simulate being placed after header
    row.layout(120.0f, 16.0f);

    Pt left = row.leftPortCenter();
    EXPECT_FLOAT_EQ(left.x, 0.0f);  // at left edge of node
    EXPECT_FLOAT_EQ(left.y, 24.0f + 8.0f);  // vertically centered in row
}

TEST(PortRowWidgetTest, RightPortCenterAtRightEdge) {
    PortRowWidget row("in", "out");
    row.setPosition(0, 24.0f);
    row.layout(120.0f, 16.0f);

    Pt right = row.rightPortCenter();
    EXPECT_FLOAT_EQ(right.x, 120.0f);  // at right edge of node
    EXPECT_FLOAT_EQ(right.y, 24.0f + 8.0f);
}

TEST(PortRowWidgetTest, EmptyLeftPort) {
    PortRowWidget row("", "out");
    row.layout(120.0f, 16.0f);

    MockDrawList dl;
    row.render(&dl, Pt(0, 0), 1.0f);
    // Should only draw right port circle
}

TEST(PortRowWidgetTest, EmptyRightPort) {
    PortRowWidget row("in", "");
    row.layout(120.0f, 16.0f);

    MockDrawList dl;
    row.render(&dl, Pt(0, 0), 1.0f);
    // Should only draw left port circle
}

TEST(PortRowWidgetTest, ContentBoundsCalculated) {
    PortRowWidget row("in", "out");
    row.layout(120.0f, 16.0f);

    Bounds content = row.contentBounds();
    EXPECT_GT(content.w, 0.0f);
    EXPECT_GT(content.x, PortRowWidget::PORT_RADIUS);
    EXPECT_LT(content.x + content.w, 120.0f - PortRowWidget::PORT_RADIUS);
}

TEST(PortRowWidgetTest, RenderDrawsCircles) {
    PortRowWidget row("in", "out");
    row.layout(120.0f, 16.0f);

    DetailedMockDrawList dl;
    row.render(&dl, Pt(100, 100), 1.0f);
    // Verify circles were drawn (add_circle_filled)
    EXPECT_EQ(dl.circles.size(), 2u);
}

// ============================================================================
// ContentWidget tests
// ============================================================================

TEST(ContentWidgetTest, IsFlexibleByDefault) {
    ContentWidget content("label");
    EXPECT_TRUE(content.isFlexible());
}

TEST(ContentWidgetTest, RenderDoesNotCrash) {
    ContentWidget content("test", 0.5f);
    content.layout(120.0f, 40.0f);

    MockDrawList dl;
    content.render(&dl, Pt(0, 0), 1.0f);
}

TEST(ContentWidgetTest, EmptyLabelDoesNotRender) {
    ContentWidget content;
    content.layout(120.0f, 40.0f);

    MockDrawList dl;
    content.render(&dl, Pt(0, 0), 1.0f);
    // Should not crash, should not draw
}

// ============================================================================
// StandardVisualNode layout integration tests
// ============================================================================

TEST(StandardVisualNodeLayoutTest, NodeWithPortsHasLayout) {
    Node n;
    n.id = "batt1";
    n.name = "Battery";
    n.type_name = "Battery";
    n.at(0, 0).size_wh(120, 80);
    n.input("v_in");
    n.output("v_out");

    StandardVisualNode visual(n);
    const auto& layout = visual.getLayout();

    // Should have: header + 1 port row + typename = 3 children
    EXPECT_EQ(layout.childCount(), 3u);
}

TEST(StandardVisualNodeLayoutTest, PortPositionsAtNodeEdges) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(96, 192).size_wh(128, 80);  // grid-aligned values
    n.input("in");
    n.output("out");

    StandardVisualNode visual(n);

    // Input port should be at left edge of node (x = snapped node.x)
    Pt in_pos = visual.getPortPosition("in");
    EXPECT_FLOAT_EQ(in_pos.x, visual.getPosition().x);  // left edge

    // Output port should be at right edge of node (x = node.x + width)
    Pt out_pos = visual.getPortPosition("out");
    EXPECT_FLOAT_EQ(out_pos.x, visual.getPosition().x + visual.getSize().x);  // right edge
}

TEST(StandardVisualNodeLayoutTest, PortsVerticallyCenteredInRows) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(0, 0).size_wh(120, 80);
    n.input("in1");
    n.input("in2");
    n.output("out1");

    StandardVisualNode visual(n);

    Pt in1 = visual.getPortPosition("in1");
    Pt in2 = visual.getPortPosition("in2");

    // Ports should be spaced by ROW_HEIGHT (16)
    EXPECT_FLOAT_EQ(in2.y - in1.y, PortRowWidget::ROW_HEIGHT);

    // First port at header height + half row
    float expected_y = HeaderWidget::HEIGHT + PortRowWidget::ROW_HEIGHT / 2;
    EXPECT_FLOAT_EQ(in1.y, expected_y);
}

TEST(StandardVisualNodeLayoutTest, NodeAutoSizesForManyPorts) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(0, 0).size_wh(120, 32);  // very small height
    n.input("in1");
    n.input("in2");
    n.input("in3");
    n.output("out1");

    StandardVisualNode visual(n);

    // Node should auto-size to fit: header(24) + 3 rows(48) + typename(16) = 88
    // Snapped to grid: ceil(88/16)*16 = 96
    EXPECT_GE(visual.getSize().y, 88.0f);
}

TEST(StandardVisualNodeLayoutTest, NodeWithContent) {
    Node n;
    n.id = "n1";
    n.name = "Switch";
    n.type_name = "switch";
    n.at(0, 0).size_wh(120, 80);
    n.input("ctrl");
    n.output("state");
    NodeContent nc;
    nc.type = NodeContentType::Switch;
    nc.label = "ON/OFF";
    n.with_content(nc);

    StandardVisualNode visual(n);

    // Should have: header + 1 port row + content + typename = 4 children
    EXPECT_EQ(visual.getLayout().childCount(), 4u);
}

TEST(StandardVisualNodeLayoutTest, RenderDoesNotCrash) {
    Node n;
    n.id = "n1";
    n.name = "Battery";
    n.type_name = "Battery";
    n.at(96, 48).size_wh(128, 80);
    n.input("v_in");
    n.output("v_out");

    StandardVisualNode visual(n);
    DetailedMockDrawList dl;
    Viewport vp;
    visual.render(&dl, vp, Pt(0, 0), false);

    // Port circles should be drawn via the layout
    EXPECT_GE(dl.circles.size(), 2u);  // at least 2 port circles
}

TEST(StandardVisualNodeLayoutTest, SelectedNodeRenders) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(0, 0).size_wh(120, 80);

    StandardVisualNode visual(n);
    MockDrawList dl;
    Viewport vp;
    visual.render(&dl, vp, Pt(0, 0), true);
    EXPECT_TRUE(dl.had_rect());
}

// ============================================================================
// PortRowWidget rendering detail tests
// ============================================================================

TEST(PortRowWidgetTest, CirclesCenteredAtEdges) {
    PortRowWidget row("in", "out");
    row.layout(120.0f, 16.0f);

    DetailedMockDrawList dl;
    Pt origin(200.0f, 300.0f);
    row.render(&dl, origin, 1.0f);

    ASSERT_EQ(dl.circles.size(), 2u);

    // Left circle: center at origin.x (left edge)
    EXPECT_FLOAT_EQ(dl.circles[0].center.x, origin.x);  // left edge
    EXPECT_FLOAT_EQ(dl.circles[0].center.y, origin.y + 8.0f);  // vertically centered

    // Right circle: center at origin.x + width (right edge)
    EXPECT_FLOAT_EQ(dl.circles[1].center.x, origin.x + 120.0f);  // right edge
    EXPECT_FLOAT_EQ(dl.circles[1].center.y, origin.y + 8.0f);
}

TEST(PortRowWidgetTest, CirclesScaleWithZoom) {
    PortRowWidget row("in", "out");
    row.layout(120.0f, 16.0f);

    DetailedMockDrawList dl;
    row.render(&dl, Pt(0, 0), 2.0f);

    ASSERT_EQ(dl.circles.size(), 2u);
    EXPECT_FLOAT_EQ(dl.circles[0].radius, PortRowWidget::PORT_RADIUS * 2.0f);
}

// ============================================================================
// Bounds tests
// ============================================================================

TEST(BoundsTest, ContainsInside) {
    Bounds b{10, 20, 100, 50};
    EXPECT_TRUE(b.contains(50, 40));
    EXPECT_TRUE(b.contains(10, 20));  // top-left corner
}

TEST(BoundsTest, ContainsOutside) {
    Bounds b{10, 20, 100, 50};
    EXPECT_FALSE(b.contains(5, 40));    // left of bounds
    EXPECT_FALSE(b.contains(50, 15));   // above bounds
    EXPECT_FALSE(b.contains(111, 40));  // right of bounds
    EXPECT_FALSE(b.contains(50, 71));   // below bounds
}
