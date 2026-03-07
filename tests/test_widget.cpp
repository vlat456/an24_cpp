#include <gtest/gtest.h>
#include <cmath>
#include "editor/interfaces.h"
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
// VisualNode layout integration tests
// ============================================================================

TEST(VisualNodeLayoutTest, NodeWithPortsHasLayout) {
    Node n;
    n.id = "batt1";
    n.name = "Battery";
    n.type_name = "Battery";
    n.at(0, 0).size_wh(120, 80);
    n.input("v_in");
    n.output("v_out");

    VisualNode visual(n);
    const auto& layout = visual.getLayout();

    // Should have: header + 1 port row + typename = 3 children
    EXPECT_EQ(layout.childCount(), 3u);
}

TEST(VisualNodeLayoutTest, PortPositionsAtNodeEdges) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(96, 192).size_wh(128, 80);  // grid-aligned values
    n.input("in");
    n.output("out");

    VisualNode visual(n);

    // Input port should be at left edge of node (x = snapped node.x)
    Pt in_pos = visual.getPort("in")->worldPosition();
    EXPECT_FLOAT_EQ(in_pos.x, visual.getPosition().x);  // left edge

    // Output port should be at right edge of node (x = node.x + width)
    Pt out_pos = visual.getPort("out")->worldPosition();
    EXPECT_FLOAT_EQ(out_pos.x, visual.getPosition().x + visual.getSize().x);  // right edge
}

TEST(VisualNodeLayoutTest, PortsVerticallyCenteredInRows) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(0, 0).size_wh(120, 80);
    n.input("in1");
    n.input("in2");
    n.output("out1");

    VisualNode visual(n);

    Pt in1 = visual.getPort("in1")->worldPosition();
    Pt in2 = visual.getPort("in2")->worldPosition();

    // Ports should be spaced by ROW_HEIGHT (16)
    EXPECT_FLOAT_EQ(in2.y - in1.y, PortRowWidget::ROW_HEIGHT);

    // First port at header height + half row
    float expected_y = HeaderWidget::HEIGHT + PortRowWidget::ROW_HEIGHT / 2;
    EXPECT_FLOAT_EQ(in1.y, expected_y);
}

TEST(VisualNodeLayoutTest, NodeAutoSizesForManyPorts) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(0, 0).size_wh(120, 32);  // very small height
    n.input("in1");
    n.input("in2");
    n.input("in3");
    n.output("out1");

    VisualNode visual(n);

    // Node should auto-size to fit: header(24) + 3 rows(48) + typename(16) = 88
    // Snapped to grid: ceil(88/16)*16 = 96
    EXPECT_GE(visual.getSize().y, 88.0f);
}

TEST(VisualNodeLayoutTest, NodeWithContent) {
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

    VisualNode visual(n);

    // Should have: header + 1 port row + content + typename = 4 children
    EXPECT_EQ(visual.getLayout().childCount(), 4u);
}

TEST(VisualNodeLayoutTest, RenderDoesNotCrash) {
    Node n;
    n.id = "n1";
    n.name = "Battery";
    n.type_name = "Battery";
    n.at(96, 48).size_wh(128, 80);
    n.input("v_in");
    n.output("v_out");

    VisualNode visual(n);
    DetailedMockDrawList dl;
    Viewport vp;
    visual.render(&dl, vp, Pt(0, 0), false);

    // Port circles should be drawn via the layout
    EXPECT_GE(dl.circles.size(), 2u);  // at least 2 port circles
}

TEST(VisualNodeLayoutTest, SelectedNodeRenders) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(0, 0).size_wh(120, 80);

    VisualNode visual(n);
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

// ============================================================================
// IDrawable interface tests
// ============================================================================

TEST(IDrawableTest, StandardNodeIsDrawable) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(0, 0).size_wh(120, 80);

    VisualNode visual(n);
    IDrawable* drawable = &visual;

    MockDrawList dl;
    Viewport vp;
    drawable->render(&dl, vp, Pt(0, 0), false);
    EXPECT_TRUE(dl.had_rect());
}

TEST(IDrawableTest, BusNodeIsDrawable) {
    Node n;
    n.id = "bus1";
    n.name = "Bus";
    n.type_name = "bus";
    n.kind = NodeKind::Bus;
    n.at(0, 0).size_wh(64, 32);

    BusVisualNode visual(n);
    IDrawable* drawable = &visual;

    MockDrawList dl;
    Viewport vp;
    drawable->render(&dl, vp, Pt(0, 0), false);
    EXPECT_TRUE(dl.had_rect());
}

TEST(IDrawableTest, RefNodeIsDrawable) {
    Node n;
    n.id = "ref1";
    n.name = "GND";
    n.type_name = "refnode";
    n.kind = NodeKind::Ref;
    n.at(0, 0).size_wh(48, 32);

    RefVisualNode visual(n);
    IDrawable* drawable = &visual;

    MockDrawList dl;
    Viewport vp;
    drawable->render(&dl, vp, Pt(0, 0), false);
    EXPECT_TRUE(dl.had_rect());
}

// ============================================================================
// ISelectable interface tests
// ============================================================================

TEST(ISelectableTest, ContainsPointInside) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(96, 192).size_wh(128, 80);

    VisualNode visual(n);
    ISelectable* selectable = &visual;

    // Point inside the node
    EXPECT_TRUE(selectable->containsPoint(Pt(150, 230)));
    // Top-left corner (grid-aligned position)
    EXPECT_TRUE(selectable->containsPoint(Pt(96, 192)));
    // Bottom-right corner
    EXPECT_TRUE(selectable->containsPoint(Pt(224, 272)));
}

TEST(ISelectableTest, ContainsPointOutside) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(96, 192).size_wh(128, 80);

    VisualNode visual(n);
    ISelectable* selectable = &visual;

    // Point outside
    EXPECT_FALSE(selectable->containsPoint(Pt(50, 192)));  // left
    EXPECT_FALSE(selectable->containsPoint(Pt(96, 100)));  // above
    EXPECT_FALSE(selectable->containsPoint(Pt(300, 200))); // right
    EXPECT_FALSE(selectable->containsPoint(Pt(100, 400))); // below
}

TEST(ISelectableTest, BusNodeSelectable) {
    Node n;
    n.id = "bus1";
    n.name = "Bus";
    n.kind = NodeKind::Bus;
    n.at(0, 0).size_wh(64, 32);

    BusVisualNode visual(n);
    ISelectable* selectable = &visual;

    EXPECT_TRUE(selectable->containsPoint(Pt(32, 16)));
    EXPECT_FALSE(selectable->containsPoint(Pt(100, 100)));
}

// ============================================================================
// IDraggable interface tests
// ============================================================================

TEST(IDraggableTest, GetSetPosition) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(96, 192).size_wh(128, 80);

    VisualNode visual(n);
    IDraggable* draggable = &visual;

    Pt pos = draggable->getPosition();
    EXPECT_FLOAT_EQ(pos.x, 96.0f);
    EXPECT_FLOAT_EQ(pos.y, 192.0f);

    draggable->setPosition(Pt(200, 300));
    EXPECT_FLOAT_EQ(draggable->getPosition().x, 200.0f);
    EXPECT_FLOAT_EQ(draggable->getPosition().y, 300.0f);
}

TEST(IDraggableTest, GetSetSize) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(0, 0).size_wh(120, 80);

    VisualNode visual(n);
    IDraggable* draggable = &visual;

    Pt size = draggable->getSize();
    EXPECT_GE(size.x, 120.0f);
    EXPECT_GE(size.y, 80.0f);

    // [t4u5v6w7] setSize now snaps to grid (16px), use grid-aligned values
    draggable->setSize(Pt(192, 160));
    EXPECT_FLOAT_EQ(draggable->getSize().x, 192.0f);
    EXPECT_FLOAT_EQ(draggable->getSize().y, 160.0f);
}

TEST(IDraggableTest, DragUpdatesSelectionBounds) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(96, 192).size_wh(128, 80);

    VisualNode visual(n);
    IDraggable* draggable = &visual;
    ISelectable* selectable = &visual;

    // Move node
    draggable->setPosition(Pt(500, 500));
    // Old position should no longer contain point
    EXPECT_FALSE(selectable->containsPoint(Pt(100, 200)));
    // New position should contain point
    EXPECT_TRUE(selectable->containsPoint(Pt(550, 530)));
}

// ============================================================================
// IPersistable interface tests
// ============================================================================

TEST(IPersistableTest, NodeHasStableId) {
    Node n;
    n.id = "battery_42";
    n.name = "Battery";
    n.type_name = "battery";
    n.at(0, 0).size_wh(120, 80);

    VisualNode visual(n);
    IPersistable* persistable = &visual;

    EXPECT_EQ(persistable->getId(), "battery_42");
}

TEST(IPersistableTest, BusNodeHasId) {
    Node n;
    n.id = "bus_7";
    n.name = "Bus";
    n.kind = NodeKind::Bus;
    n.at(0, 0).size_wh(64, 32);

    BusVisualNode visual(n);
    IPersistable* persistable = &visual;

    EXPECT_EQ(persistable->getId(), "bus_7");
}

TEST(IPersistableTest, RefNodeHasId) {
    Node n;
    n.id = "gnd_1";
    n.name = "GND";
    n.kind = NodeKind::Ref;
    n.at(0, 0).size_wh(48, 32);

    RefVisualNode visual(n);
    IPersistable* persistable = &visual;

    EXPECT_EQ(persistable->getId(), "gnd_1");
}

// ============================================================================
// Content access tests
// ============================================================================

TEST(ContentAccessTest, StandardNodeWithContent) {
    Node n;
    n.id = "sw1";
    n.name = "Switch";
    n.type_name = "switch";
    n.at(0, 0).size_wh(120, 80);
    n.input("ctrl");
    n.output("state");
    NodeContent nc;
    nc.type = NodeContentType::Switch;
    nc.label = "ON/OFF";
    n.with_content(nc);

    VisualNode visual(n);
    VisualNode* base = &visual;

    EXPECT_EQ(base->getContentType(), NodeContentType::Switch);
    EXPECT_EQ(base->getNodeContent().label, "ON/OFF");

    Bounds cb = base->getContentBounds();
    EXPECT_GT(cb.w, 0.0f);
    EXPECT_GT(cb.h, 0.0f);
}

TEST(ContentAccessTest, NodeWithoutContentReturnsNone) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(0, 0).size_wh(120, 80);

    VisualNode visual(n);
    VisualNode* base = &visual;

    EXPECT_EQ(base->getContentType(), NodeContentType::None);
}

TEST(ContentAccessTest, BusNodeContentDefaultsToNone) {
    Node n;
    n.id = "bus1";
    n.name = "Bus";
    n.kind = NodeKind::Bus;
    n.at(0, 0).size_wh(64, 32);

    BusVisualNode visual(n);
    VisualNode* base = &visual;

    EXPECT_EQ(base->getContentType(), NodeContentType::None);
}

// ============================================================================
// Multiple interface cast tests (ISP compliance)
// ============================================================================

TEST(InterfaceCastTest, NodeSupportsAllInterfaces) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    n.at(0, 0).size_wh(120, 80);

    VisualNode visual(n);

    // All four interfaces accessible
    IDrawable* drawable = dynamic_cast<IDrawable*>(&visual);
    ISelectable* selectable = dynamic_cast<ISelectable*>(&visual);
    IDraggable* draggable = dynamic_cast<IDraggable*>(&visual);
    IPersistable* persistable = dynamic_cast<IPersistable*>(&visual);

    EXPECT_NE(drawable, nullptr);
    EXPECT_NE(selectable, nullptr);
    EXPECT_NE(draggable, nullptr);
    EXPECT_NE(persistable, nullptr);
}

TEST(InterfaceCastTest, FactoryCreatedNodeSupportsInterfaces) {
    Node n;
    n.id = "n1";
    n.name = "Pump";
    n.type_name = "pump";
    n.at(0, 0).size_wh(120, 80);
    n.input("v_in");
    n.output("v_out");

    auto visual = VisualNodeFactory::create(n);

    EXPECT_NE(dynamic_cast<IDrawable*>(visual.get()), nullptr);
    EXPECT_NE(dynamic_cast<ISelectable*>(visual.get()), nullptr);
    EXPECT_NE(dynamic_cast<IDraggable*>(visual.get()), nullptr);
    EXPECT_NE(dynamic_cast<IPersistable*>(visual.get()), nullptr);
}

// ============================================================================
// [t4u5v6w7] Regression: setSize snaps to grid so bottom-right is grid-aligned
// ============================================================================

TEST(VisualNodeTest, SetSize_SnapsToGrid) {
    Node n;
    n.id = "n1"; n.name = "N"; n.type_name = "T";
    n.at(0, 0).size_wh(120, 80);
    n.input("a"); n.output("b");

    VisualNode visual(n);

    // Set non-grid-aligned size → should ceil-snap to next grid (16px) boundary
    visual.setSize(Pt(130.0f, 90.0f));
    Pt s = visual.getSize();
    // 130 → ceil(130/16)*16 = 144
    EXPECT_FLOAT_EQ(s.x, 144.0f);
    // 90  → ceil(90/16)*16  = 96
    EXPECT_FLOAT_EQ(s.y, 96.0f);

    // Already grid-aligned size stays unchanged
    visual.setSize(Pt(128.0f, 96.0f));
    s = visual.getSize();
    EXPECT_FLOAT_EQ(s.x, 128.0f);
    EXPECT_FLOAT_EQ(s.y, 96.0f);
}

TEST(VisualNodeTest, SetSize_BusAcceptsExternal) {
    Node n;
    n.id = "bus1"; n.name = "bus"; n.kind = NodeKind::Bus;
    n.at(0, 0).size_wh(80, 32);
    n.input("v"); n.output("v");

    BusVisualNode visual(n, BusOrientation::Horizontal);

    // Bus uses base class setSize(), so external size is accepted
    visual.setSize(Pt(999.0f, 999.0f));
    Pt after = visual.getSize();
    EXPECT_GT(after.x, 0.0f) << "Bus should have positive size";
    EXPECT_GT(after.y, 0.0f) << "Bus should have positive size";
}

// ============================================================================
// [x8y9z0a1] Regression: getContentBounds returns symmetric (centred) area
// ============================================================================

TEST(VisualNodeTest, ContentBounds_SymmetricMargins) {
    Node n;
    n.id = "n1"; n.name = "Switch1"; n.type_name = "Switch";
    n.at(0, 0).size_wh(128, 96);
    n.input("control");   // 7 chars - wider label
    n.output("state");     // 5 chars - narrower label
    n.node_content.type = NodeContentType::Switch;
    n.node_content.label = "ON";

    VisualNode visual(n);
    Bounds cb = visual.getContentBounds();

    // Content area must be centred: left margin == right margin
    float left_margin  = cb.x;
    float right_margin = visual.getSize().x - (cb.x + cb.w);

    EXPECT_NEAR(left_margin, right_margin, 0.01f)
        << "Content area should be horizontally centred within the node";
    EXPECT_GT(cb.w, 0.0f);
}

// ============================================================================
// [i3j4k5l6] Regression: Bus alias ports come first, "v" port at end
// ============================================================================

TEST(VisualNodeTest, BusPortOrder_AliasFirst_VLast) {
    Node bus;
    bus.id = "bus1"; bus.name = "bus"; bus.kind = NodeKind::Bus;
    bus.at(200, 100).size_wh(80, 32);
    bus.input("v"); bus.output("v");

    // Two wires attached to the bus
    Wire w1 = Wire::make("w1", wire_output("a", "o"), wire_input("bus1", "v"));
    Wire w2 = Wire::make("w2", wire_output("b", "o"), wire_input("bus1", "v"));
    std::vector<Wire> wires = {w1, w2};

    BusVisualNode visual(bus, BusOrientation::Horizontal, wires);

    // Should have 3 ports: w1 alias, w2 alias, "v" at the end
    EXPECT_EQ(visual.getPortCount(), 3u);

    auto* p0 = visual.getPort(size_t(0));
    auto* p1 = visual.getPort(size_t(1));
    auto* p2 = visual.getPort(size_t(2));

    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);

    // First two are alias ports (named after wire IDs)
    EXPECT_EQ(p0->name(), "w1");
    EXPECT_EQ(p1->name(), "w2");
    // Last is the logical "v" port
    EXPECT_EQ(p2->name(), "v");
}

TEST(VisualNodeTest, BusPortOrder_NoWires_SingleVPort) {
    Node bus;
    bus.id = "bus1"; bus.name = "bus"; bus.kind = NodeKind::Bus;
    bus.at(200, 100).size_wh(80, 32);
    bus.input("v"); bus.output("v");

    BusVisualNode visual(bus, BusOrientation::Horizontal);

    // No wires → just the "v" port
    EXPECT_EQ(visual.getPortCount(), 1u);
    auto* p0 = visual.getPort(size_t(0));
    ASSERT_NE(p0, nullptr);
    EXPECT_EQ(p0->name(), "v");
}

// ============================================================================
// [d5e6f7g8] Regression: RefNode port position is grid-aligned
// ============================================================================

TEST(RefNodeGridTest, PortPosition_IsGridAligned) {
    constexpr float GRID = 16.0f;

    // Test several positions — all should yield grid-snapped port
    float positions[] = {0.0f, 16.0f, 32.0f, 48.0f, 64.0f, 128.0f};
    for (float px : positions) {
        Node n;
        n.id = "ref1"; n.name = "GND"; n.kind = NodeKind::Ref;
        n.at(px, px).size_wh(40, 40);
        n.output("v");
        RefVisualNode visual(n);

        Pt port = visual.getPort("v")->worldPosition();
        float rx = std::fmod(port.x, GRID);
        float ry = std::fmod(port.y, GRID);
        EXPECT_NEAR(rx, 0.0f, 0.01f)
            << "Port X not grid-aligned at node pos " << px;
        EXPECT_NEAR(ry, 0.0f, 0.01f)
            << "Port Y not grid-aligned at node pos " << px;
    }
}

TEST(StandardNodeGridTest, PortPositions_AreGridAligned) {
    constexpr float GRID = 16.0f;

    Node n;
    n.id = "gen"; n.name = "Gen"; n.kind = NodeKind::Node;
    n.at(64.0f, 64.0f).size_wh(120, 80);
    n.input("a"); n.input("b");
    n.output("c"); n.output("d");

    VisualNode visual(n);
    for (const auto& pname : visual.getPortNames()) {
        Pt port = visual.getPort(pname)->worldPosition();
        float rx = std::fmod(port.x, GRID);
        float ry = std::fmod(port.y, GRID);
        EXPECT_NEAR(rx, 0.0f, 0.01f)
            << "Port '" << pname << "' X not grid-aligned";
        EXPECT_NEAR(ry, 0.0f, 0.01f)
            << "Port '" << pname << "' Y not grid-aligned";
    }
}

TEST(BusNodeGridTest, PortPositions_AreGridAligned) {
    constexpr float GRID = 16.0f;

    Node n;
    n.id = "bus1"; n.name = "bus"; n.kind = NodeKind::Bus;
    n.at(48.0f, 48.0f).size_wh(80, 32);
    n.input("v"); n.output("v");

    std::vector<Wire> wires;
    wires.push_back(Wire::make("w1", wire_output("a", "o"), wire_input("bus1", "v")));
    wires.push_back(Wire::make("w2", wire_output("b", "o"), wire_input("bus1", "v")));

    BusVisualNode visual(n, BusOrientation::Horizontal, wires);
    for (size_t i = 0; i < visual.getPortCount(); i++) {
        auto* p = visual.getPort(i);
        ASSERT_NE(p, nullptr);
        Pt port = visual.getPort(p->name())->worldPosition();
        float rx = std::fmod(port.x, GRID);
        float ry = std::fmod(port.y, GRID);
        EXPECT_NEAR(rx, 0.0f, 0.01f)
            << "Bus port '" << p->name() << "' X not grid-aligned";
        EXPECT_NEAR(ry, 0.0f, 0.01f)
            << "Bus port '" << p->name() << "' Y not grid-aligned";
    }
}
