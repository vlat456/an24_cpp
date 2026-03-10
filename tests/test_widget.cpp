#include <gtest/gtest.h>
#include <cmath>
#include "editor/visual/interfaces.h"
#include "editor/visual/node/widget.h"
#include "editor/visual/node/layout.h"
#include "editor/visual/node/node.h"
#include "editor/visual/renderer/draw_list.h"
#include "editor/visual/renderer/mock_draw_list.h"
#include "editor/data/node.h"
#include "editor/layout_constants.h"
#include "editor/visual/renderer/render_theme.h"

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
    void add_rect_with_rounding_corners(Pt, Pt, uint32_t, float, int, float = 1.0f) override { had_rect_ = true; }
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
    void add_rect_filled_with_rounding(Pt, Pt, uint32_t, float) override { had_rect_ = true; }
    void add_rect_filled_with_rounding_corners(Pt, Pt, uint32_t, float, int) override { had_rect_ = true; }
};

// ============================================================================
// Widget base tests
// ============================================================================

TEST(WidgetTest, DefaultState) {
    // Can't instantiate Widget directly (abstract), but test via Column
    Column layout;
    EXPECT_FLOAT_EQ(layout.x(), 0.0f);
    EXPECT_FLOAT_EQ(layout.y(), 0.0f);
    EXPECT_FLOAT_EQ(layout.width(), 0.0f);
    EXPECT_FLOAT_EQ(layout.height(), 0.0f);
    EXPECT_FALSE(layout.isFlexible());
}

TEST(WidgetTest, SetPosition) {
    Column layout;
    layout.setPosition(10.0f, 20.0f);
    EXPECT_FLOAT_EQ(layout.x(), 10.0f);
    EXPECT_FLOAT_EQ(layout.y(), 20.0f);
}

TEST(WidgetTest, SetSize) {
    Column layout;
    layout.setSize(100.0f, 50.0f);
    Pt sz = layout.getSize();
    EXPECT_FLOAT_EQ(sz.x, 100.0f);
    EXPECT_FLOAT_EQ(sz.y, 50.0f);
}

TEST(WidgetTest, GetBounds) {
    Column layout;
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

// ColumnLayout tests removed — superseded by ColumnTest in test_layout.cpp

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

// PortRowWidget tests removed — ports now composed from Row/Container/Circle/Label

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

    // Ports should be spaced by PORT_ROW_HEIGHT (16)
    EXPECT_FLOAT_EQ(in2.y - in1.y, editor_constants::PORT_ROW_HEIGHT);

    // First port at header height + half row
    float expected_y = HeaderWidget::HEIGHT + editor_constants::PORT_ROW_HEIGHT / 2;
    EXPECT_FLOAT_EQ(in1.y, expected_y);
}

TEST(VisualNodeLayoutTest, NodeAutoSizesForManyPorts) {
    Node n;
    n.id = "n1";
    n.name = "Test";
    n.type_name = "test";
    // Don't set explicit size - let it auto-size
    n.at(0, 0);
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
    n.render_hint = "bus";
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
    n.render_hint = "ref";
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
    // Use non-default size to ensure it's treated as explicit
    n.at(96, 192).size_wh(128, 81);

    VisualNode visual(n);
    ISelectable* selectable = &visual;

    // Point inside the node
    EXPECT_TRUE(selectable->containsPoint(Pt(150, 230)));
    // Top-left corner (grid-aligned position)
    EXPECT_TRUE(selectable->containsPoint(Pt(96, 192)));
    // Bottom-right corner
    EXPECT_TRUE(selectable->containsPoint(Pt(224, 273)));
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
    n.render_hint = "bus";
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
    // Use non-default size to ensure it's treated as explicit
    n.at(0, 0).size_wh(121, 81);

    VisualNode visual(n);
    IDraggable* draggable = &visual;

    Pt size = draggable->getSize();
    EXPECT_FLOAT_EQ(size.x, 121.0f);
    EXPECT_FLOAT_EQ(size.y, 81.0f);

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
    n.render_hint = "bus";
    n.at(0, 0).size_wh(64, 32);

    BusVisualNode visual(n);
    IPersistable* persistable = &visual;

    EXPECT_EQ(persistable->getId(), "bus_7");
}

TEST(IPersistableTest, RefNodeHasId) {
    Node n;
    n.id = "gnd_1";
    n.name = "GND";
    n.render_hint = "ref";
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
    n.render_hint = "bus";
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
    n.id = "bus1"; n.name = "bus"; n.render_hint = "bus";
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
    bus.id = "bus1"; bus.name = "bus"; bus.render_hint = "bus";
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
    bus.id = "bus1"; bus.name = "bus"; bus.render_hint = "bus";
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
        n.id = "ref1"; n.name = "GND"; n.render_hint = "ref";
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
    n.at(64.0f, 64.0f);  // Don't set explicit size - let it auto-size
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
    n.id = "bus1"; n.name = "bus"; n.render_hint = "bus";
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

// ============================================================================
// Gauge animation regression tests
// Verifies that updateNodeContent() propagates the value to VoltmeterWidget.
// Before the fix, the value was only stored in node_content_ but never pushed
// to the widget — so the gauge needle stayed frozen at its initial position.
// ============================================================================

static Node make_gauge_node() {
    Node n;
    n.id = "v1";
    n.name = "Voltmeter";
    n.type_name = "Voltmeter";
    n.at(0, 0);  // auto-size
    n.input("v_in");
    n.node_content.type  = NodeContentType::Gauge;
    n.node_content.value = 0.0f;
    n.node_content.min   = 0.0f;
    n.node_content.max   = 30.0f;
    n.node_content.unit  = "V";
    return n;
}

// updateNodeContent() must push the new value to the VoltmeterWidget inside the layout.
TEST(GaugeAnimation, UpdateNodeContent_PropagatesValueToWidget) {
    Node n = make_gauge_node();
    VisualNode vn(n);

    // Find the VoltmeterWidget in the layout
    VoltmeterWidget* vw = nullptr;
    for (size_t i = 0; i < vn.getLayout().childCount(); i++) {
        vw = dynamic_cast<VoltmeterWidget*>(vn.getLayout().child(i));
        if (vw) break;
    }
    ASSERT_NE(vw, nullptr) << "Gauge node should have a VoltmeterWidget in layout";
    EXPECT_FLOAT_EQ(vw->getValue(), 0.0f) << "Initial value should be 0";

    // Simulate a voltage update arriving from the simulation
    NodeContent updated = n.node_content;
    updated.value = 15.5f;
    vn.updateNodeContent(updated);

    // The widget value must have been updated (regression: was NOT before the fix)
    EXPECT_FLOAT_EQ(vw->getValue(), 15.5f)
        << "VoltmeterWidget value must match after updateNodeContent() — "
           "was broken by rounded-corners refactor (gauge needle frozen at 0)";
}

// Multiple sequential updates must all propagate correctly.
TEST(GaugeAnimation, UpdateNodeContent_MultipleUpdates_AllPropagate) {
    Node n = make_gauge_node();
    VisualNode vn(n);

    VoltmeterWidget* vw = nullptr;
    for (size_t i = 0; i < vn.getLayout().childCount(); i++) {
        vw = dynamic_cast<VoltmeterWidget*>(vn.getLayout().child(i));
        if (vw) break;
    }
    ASSERT_NE(vw, nullptr);

    for (float v : {5.0f, 10.0f, 0.0f, 29.9f, 15.0f}) {
        NodeContent c = n.node_content;
        c.value = v;
        vn.updateNodeContent(c);
        EXPECT_FLOAT_EQ(vw->getValue(), v) << "Value " << v << " not propagated";
    }
}

// updateNodeContent() for a non-gauge node should not crash.
TEST(GaugeAnimation, UpdateNodeContent_NonGauge_NoSideEffects) {
    Node n;
    n.id = "b1"; n.name = "Battery"; n.type_name = "Battery";
    n.at(0, 0).size_wh(96, 80);
    n.output("v_out");
    n.node_content.type = NodeContentType::None;

    VisualNode vn(n);
    NodeContent c = n.node_content;
    c.label = "updated";
    EXPECT_NO_FATAL_FAILURE(vn.updateNodeContent(c));
}

// Gauge renders correctly after an update (arc + needle draw calls expected).
TEST(GaugeAnimation, Render_AfterUpdate_DoesNotCrash) {
    Node n = make_gauge_node();
    VisualNode vn(n);

    NodeContent updated = n.node_content;
    updated.value = 24.0f;  // near max
    vn.updateNodeContent(updated);

    MockDrawList dl;
    Viewport vp;
    EXPECT_NO_FATAL_FAILURE(vn.render(&dl, vp, Pt(0, 0), false));
    // Gauge arc is a polyline; needle is drawn as a line
    EXPECT_TRUE(dl.had_polyline_) << "Gauge arc should produce polyline draw calls";
}

// ============================================================================
// Rounded corners regression tests
// Verifies that VisualNode and HeaderWidget use the new rounding API correctly.
// ============================================================================

// Track rounding draw calls vs plain fill calls.
class RoundingTrackingDrawList : public IDrawList {
public:
    int rounded_filled_calls = 0;
    int plain_filled_calls   = 0;
    int rounded_border_calls = 0;

    void add_line(Pt, Pt, uint32_t, float) override {}
    void add_rect(Pt, Pt, uint32_t, float) override {}
    void add_rect_with_rounding_corners(Pt, Pt, uint32_t, float, int, float) override {
        rounded_border_calls++;
    }
    void add_rect_filled(Pt, Pt, uint32_t) override {
        plain_filled_calls++;
    }
    void add_rect_filled_with_rounding(Pt, Pt, uint32_t, float) override {
        rounded_filled_calls++;
    }
    void add_rect_filled_with_rounding_corners(Pt, Pt, uint32_t, float, int) override {
        rounded_filled_calls++;
    }
    void add_circle(Pt, float, uint32_t, int) override {}
    void add_circle_filled(Pt, float, uint32_t, int) override {}
    void add_text(Pt, const char*, uint32_t, float) override {}
    Pt calc_text_size(const char* t, float fs) const override {
        return Pt(strlen(t) * fs * 0.6f, fs);
    }
    void add_polyline(const Pt*, size_t, uint32_t, float) override {}
};

// VisualNode::render() must use rounded-rect APIs for background and border.
TEST(RoundedCorners, VisualNode_Render_UsesRoundedBackground) {
    Node n;
    n.id = "r1"; n.name = "R"; n.type_name = "R";
    n.at(0, 0).size_wh(96, 80);
    n.input("v_in"); n.output("v_out");

    VisualNode vn(n);
    RoundingTrackingDrawList dl;
    Viewport vp;
    vn.render(&dl, vp, Pt(0, 0), false);

    EXPECT_GT(dl.rounded_filled_calls, 0)
        << "VisualNode::render() must call add_rect_filled_with_rounding[_corners]";
    EXPECT_GT(dl.rounded_border_calls, 0)
        << "VisualNode::render() must call add_rect_with_rounding_corners for border";
}

// HeaderWidget with rounding > 0 must use add_rect_filled_with_rounding_corners.
TEST(RoundedCorners, HeaderWidget_WithRounding_UsesRoundedFill) {
    Column layout;
    layout.addChild(std::make_unique<HeaderWidget>(
        "Node", render_theme::COLOR_HEADER_FILL, editor_constants::NODE_ROUNDING));
    layout.layout(120.0f, 80.0f);

    RoundingTrackingDrawList dl;
    layout.render(&dl, Pt(0, 0), 1.0f);

    EXPECT_GT(dl.rounded_filled_calls, 0)
        << "HeaderWidget with rounding must call add_rect_filled_with_rounding_corners";
    EXPECT_EQ(dl.plain_filled_calls, 0)
        << "HeaderWidget with rounding must NOT call plain add_rect_filled";
}

// HeaderWidget with default rounding (0) must use the plain add_rect_filled.
TEST(RoundedCorners, HeaderWidget_NoRounding_UsesPlainFill) {
    Column layout;
    layout.addChild(std::make_unique<HeaderWidget>(
        "Node", render_theme::COLOR_HEADER_FILL));
    layout.layout(120.0f, 80.0f);

    RoundingTrackingDrawList dl;
    layout.render(&dl, Pt(0, 0), 1.0f);

    EXPECT_EQ(dl.rounded_filled_calls, 0)
        << "HeaderWidget without rounding must NOT call rounding variant";
    EXPECT_GT(dl.plain_filled_calls, 0)
        << "HeaderWidget without rounding must call plain add_rect_filled";
}

// BusVisualNode render must also use rounded APIs.
TEST(RoundedCorners, BusVisualNode_Render_UsesRoundedBackground) {
    Node n;
    n.id = "bus1"; n.name = "Bus"; n.type_name = "Bus";
    n.render_hint = "bus"; n.at(0, 0).size_wh(80, 32);
    n.input("v"); n.output("v");

    Wire w = Wire::make("w1", wire_output("a", "out"), wire_input("bus1", "v"));
    BusVisualNode vn(n, BusOrientation::Horizontal, {w});

    RoundingTrackingDrawList dl;
    Viewport vp;
    vn.render(&dl, vp, Pt(0, 0), false);

    EXPECT_GT(dl.rounded_filled_calls, 0)
        << "BusVisualNode::render() must use rounded fill";
    EXPECT_GT(dl.rounded_border_calls, 0)
        << "BusVisualNode::render() must use rounded border";
}

// VisualNode with custom color must still use rounded background (not plain fill).
TEST(RoundedCorners, VisualNode_CustomColor_StillRounded) {
    Node n;
    n.id = "c1"; n.name = "C"; n.type_name = "C";
    n.at(0, 0).size_wh(96, 80);
    n.input("v_in"); n.output("v_out");
    n.color = NodeColor{1.0f, 0.0f, 0.0f, 1.0f};

    VisualNode vn(n);
    RoundingTrackingDrawList dl;
    Viewport vp;
    vn.render(&dl, vp, Pt(0, 0), false);

    EXPECT_GT(dl.rounded_filled_calls, 0)
        << "VisualNode with custom color must still use rounded background";
}

// ============================================================================
// Regression: port row container height must be exactly PORT_ROW_HEIGHT
// Before fix, buildLayout used PORT_RADIUS*2 (8) to compute padding, but the
// actual row preferred height is 9 (Label font_size=9 > circle diameter=8).
// This made containers 17px tall instead of 16, breaking grid alignment.
// ============================================================================

TEST(PortRowHeight, RowContainer_IsExactlyPortRowHeight) {
    Node n;
    n.id = "n1"; n.name = "Test"; n.type_name = "test";
    n.at(0, 0).size_wh(120, 80);
    n.input("in1");
    n.input("in2");
    n.output("out1");

    VisualNode visual(n);

    Pt in1 = visual.getPort("in1")->worldPosition();
    Pt in2 = visual.getPort("in2")->worldPosition();
    Pt out1 = visual.getPort("out1")->worldPosition();

    // Row-to-row spacing must be exactly PORT_ROW_HEIGHT (16), not 17.
    float dy_in = in2.y - in1.y;
    EXPECT_FLOAT_EQ(dy_in, editor_constants::PORT_ROW_HEIGHT)
        << "Adjacent port row spacing must equal PORT_ROW_HEIGHT";

    // First port Y must be integer (grid-aligned), not .5
    float first_port_y = in1.y;
    EXPECT_FLOAT_EQ(std::fmod(first_port_y, 1.0f), 0.0f)
        << "First port Y must be integer (no .5 offset from odd row height)";
}
