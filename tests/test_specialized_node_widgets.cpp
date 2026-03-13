#include <gtest/gtest.h>
#include "visual/node/ref_node_widget.h"
#include "visual/node/text_node_widget.h"
#include "visual/node/group_node_widget.h"
#include "visual/node/bus_node_widget.h"
#include "visual/node/node_factory.h"
#include "visual/scene.h"
#include "data/node.h"
#include "data/wire.h"
#include "editor/layout_constants.h"

// ============================================================================
// RefNodeWidget
// ============================================================================

TEST(RefNodeWidget, ConstructFromNode) {
    Node node;
    node.id = "gnd1";
    node.name = "GND";
    node.type_name = "RefNode";
    node.render_hint = "ref";
    node.output("v", PortType::V);

    visual::RefNodeWidget rw(node);

    EXPECT_EQ(rw.nodeId(), "gnd1");
    EXPECT_EQ(rw.name(), "GND");
    EXPECT_TRUE(rw.isClickable());
    EXPECT_NE(rw.port(), nullptr);
    EXPECT_EQ(rw.port()->name(), "v");
    EXPECT_EQ(rw.port()->type(), PortType::V);
}

TEST(RefNodeWidget, SinglePortCenteredOnTopEdge) {
    Node node;
    node.id = "gnd1";
    node.name = "GND";
    node.type_name = "RefNode";
    node.render_hint = "ref";
    node.output("v", PortType::V);
    node.at(100, 200);

    visual::RefNodeWidget rw(node);

    auto* p = rw.port();
    ASSERT_NE(p, nullptr);

    // Port's localPos() is the top-left of its bounding box (size = PORT_RADIUS*2).
    // The circle center should be at (size().x / 2, 0), so the bounding-box
    // origin is offset by -PORT_RADIUS in both axes.
    Pt port_local = p->localPos();
    constexpr float R = editor_constants::PORT_RADIUS;
    EXPECT_FLOAT_EQ(port_local.x, rw.size().x / 2.0f - R);
    // Port circle center should be at top edge (y=0), so box origin is at -R
    EXPECT_FLOAT_EQ(port_local.y, -R);
}

TEST(RefNodeWidget, UsesFirstInputIfNoOutputs) {
    Node node;
    node.id = "ref1";
    node.name = "Ref";
    node.type_name = "RefNode";
    node.render_hint = "ref";
    node.input("sig", PortType::I);

    visual::RefNodeWidget rw(node);

    ASSERT_NE(rw.port(), nullptr);
    EXPECT_EQ(rw.port()->name(), "sig");
    EXPECT_EQ(rw.port()->type(), PortType::I);
}

TEST(RefNodeWidget, DefaultPortWhenNoPorts) {
    Node node;
    node.id = "ref1";
    node.name = "Ref";
    node.type_name = "RefNode";
    node.render_hint = "ref";

    visual::RefNodeWidget rw(node);

    ASSERT_NE(rw.port(), nullptr);
    EXPECT_EQ(rw.port()->name(), "v");
}

TEST(RefNodeWidget, FindPortByName) {
    Node node;
    node.id = "gnd1";
    node.name = "GND";
    node.type_name = "RefNode";
    node.render_hint = "ref";
    node.output("v", PortType::V);

    visual::RefNodeWidget rw(node);

    EXPECT_NE(rw.port("v"), nullptr);
    EXPECT_EQ(rw.port("nonexistent"), nullptr);
}

TEST(RefNodeWidget, SizeSnappedToGrid) {
    Node node;
    node.id = "gnd1";
    node.name = "GND";
    node.type_name = "RefNode";
    node.render_hint = "ref";

    visual::RefNodeWidget rw(node);

    float grid = 16.0f;
    EXPECT_FLOAT_EQ(std::fmod(rw.size().x, grid), 0.0f);
    EXPECT_FLOAT_EQ(std::fmod(rw.size().y, grid), 0.0f);
}

TEST(RefNodeWidget, AddToScene) {
    visual::Scene scene;

    Node node;
    node.id = "gnd1";
    node.name = "GND";
    node.type_name = "RefNode";
    node.render_hint = "ref";
    node.output("v", PortType::V);

    auto ptr = std::make_unique<visual::RefNodeWidget>(node);
    auto* rw = ptr.get();
    scene.add(std::move(ptr));

    EXPECT_EQ(scene.find("gnd1"), rw);
}

// ============================================================================
// TextNodeWidget
// ============================================================================

TEST(TextNodeWidget, ConstructFromNode) {
    Node node;
    node.id = "txt1";
    node.name = "Note";
    node.type_name = "TextNode";
    node.render_hint = "text";
    node.params["text"] = "Hello World";

    visual::TextNodeWidget tw(node);

    EXPECT_EQ(tw.nodeId(), "txt1");
    EXPECT_EQ(tw.name(), "Note");
    EXPECT_EQ(tw.text(), "Hello World");
    EXPECT_TRUE(tw.isClickable());
    EXPECT_TRUE(tw.isResizable());
    EXPECT_EQ(tw.renderLayer(), visual::RenderLayer::Text);
}

TEST(TextNodeWidget, FontSizeParsing) {
    {
        Node node;
        node.id = "t1"; node.name = "T"; node.type_name = "TextNode";
        node.render_hint = "text";
        node.params["font_size"] = "small";
        visual::TextNodeWidget tw(node);
        EXPECT_FLOAT_EQ(tw.baseFontSize(), 9.0f);
    }
    {
        Node node;
        node.id = "t2"; node.name = "T"; node.type_name = "TextNode";
        node.render_hint = "text";
        node.params["font_size"] = "medium";
        visual::TextNodeWidget tw(node);
        EXPECT_FLOAT_EQ(tw.baseFontSize(), 12.0f);
    }
    {
        Node node;
        node.id = "t3"; node.name = "T"; node.type_name = "TextNode";
        node.render_hint = "text";
        // No font_size param -> default large
        visual::TextNodeWidget tw(node);
        EXPECT_FLOAT_EQ(tw.baseFontSize(), 14.0f);
    }
}

TEST(TextNodeWidget, EmptyTextDefault) {
    Node node;
    node.id = "txt1";
    node.name = "Note";
    node.type_name = "TextNode";
    node.render_hint = "text";

    visual::TextNodeWidget tw(node);

    EXPECT_TRUE(tw.text().empty());
}

TEST(TextNodeWidget, SetText) {
    Node node;
    node.id = "txt1";
    node.name = "Note";
    node.type_name = "TextNode";
    node.render_hint = "text";

    visual::TextNodeWidget tw(node);

    tw.setText("Updated");
    EXPECT_EQ(tw.text(), "Updated");
}

TEST(TextNodeWidget, SizeSnappedToGrid) {
    Node node;
    node.id = "txt1";
    node.name = "Note";
    node.type_name = "TextNode";
    node.render_hint = "text";

    visual::TextNodeWidget tw(node);

    float grid = 16.0f;
    EXPECT_FLOAT_EQ(std::fmod(tw.size().x, grid), 0.0f);
    EXPECT_FLOAT_EQ(std::fmod(tw.size().y, grid), 0.0f);
}

// ============================================================================
// GroupNodeWidget
// ============================================================================

TEST(GroupNodeWidget, ConstructFromNode) {
    Node node;
    node.id = "grp1";
    node.name = "Power Section";
    node.type_name = "Group";
    node.render_hint = "group";
    node.size_wh(200, 150);

    visual::GroupNodeWidget gw(node);

    EXPECT_EQ(gw.nodeId(), "grp1");
    EXPECT_EQ(gw.name(), "Power Section");
    EXPECT_TRUE(gw.isClickable());
    EXPECT_TRUE(gw.isResizable());
    EXPECT_EQ(gw.renderLayer(), visual::RenderLayer::Group);
}

TEST(GroupNodeWidget, EnforcesMinimumSize) {
    Node node;
    node.id = "grp1";
    node.name = "G";
    node.type_name = "Group";
    node.render_hint = "group";
    node.size_wh(10, 10);  // Smaller than minimum

    visual::GroupNodeWidget gw(node);

    EXPECT_GE(gw.size().x, 64.0f);
    EXPECT_GE(gw.size().y, 64.0f);
}

TEST(GroupNodeWidget, BorderHitTestTitleBar) {
    Node node;
    node.id = "grp1";
    node.name = "G";
    node.type_name = "Group";
    node.render_hint = "group";
    node.size_wh(208, 208);  // Pre-snapped to grid
    node.at(100, 100);

    visual::GroupNodeWidget gw(node);

    // Title bar should be clickable
    EXPECT_TRUE(gw.containsBorder(Pt(150, 105)));
}

TEST(GroupNodeWidget, BorderHitTestInterior) {
    Node node;
    node.id = "grp1";
    node.name = "G";
    node.type_name = "Group";
    node.render_hint = "group";
    node.size_wh(208, 208);  // Pre-snapped to grid
    node.at(100, 100);

    visual::GroupNodeWidget gw(node);

    // Interior (well inside borders and below title) should NOT be clickable
    EXPECT_FALSE(gw.containsBorder(Pt(200, 250)));
}

TEST(GroupNodeWidget, BorderHitTestEdges) {
    Node node;
    node.id = "grp1";
    node.name = "G";
    node.type_name = "Group";
    node.render_hint = "group";
    node.size_wh(208, 208);  // Pre-snapped to grid (16)
    node.at(100, 100);

    visual::GroupNodeWidget gw(node);

    // Verify size
    EXPECT_FLOAT_EQ(gw.size().x, 208.0f);
    EXPECT_FLOAT_EQ(gw.size().y, 208.0f);

    // Left edge (within GROUP_BORDER_HIT_MARGIN=6 of x=100)
    EXPECT_TRUE(gw.containsBorder(Pt(102, 250)));
    // Right edge (within margin of x=308)
    EXPECT_TRUE(gw.containsBorder(Pt(306, 250)));
    // Bottom edge (within margin of y=308)
    EXPECT_TRUE(gw.containsBorder(Pt(200, 306)));
}

TEST(GroupNodeWidget, BorderHitTestOutside) {
    Node node;
    node.id = "grp1";
    node.name = "G";
    node.type_name = "Group";
    node.render_hint = "group";
    node.size_wh(208, 208);  // Pre-snapped to grid
    node.at(100, 100);

    visual::GroupNodeWidget gw(node);

    // Outside completely
    EXPECT_FALSE(gw.containsBorder(Pt(50, 50)));
    EXPECT_FALSE(gw.containsBorder(Pt(400, 400)));
}

TEST(GroupNodeWidget, CustomColor) {
    Node node;
    node.id = "grp1";
    node.name = "G";
    node.type_name = "Group";
    node.render_hint = "group";
    node.color = NodeColor{0.2f, 0.4f, 0.6f, 1.0f};

    visual::GroupNodeWidget gw(node);

    EXPECT_TRUE(gw.customColor().has_value());
}

// ============================================================================
// BusNodeWidget
// ============================================================================

TEST(BusNodeWidget, ConstructWithNoWires) {
    Node node;
    node.id = "bus1";
    node.name = "Main Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    visual::BusNodeWidget bw(node);

    EXPECT_EQ(bw.nodeId(), "bus1");
    EXPECT_EQ(bw.name(), "Main Bus");
    EXPECT_TRUE(bw.isClickable());

    // Should have exactly 1 port (the base "v" port)
    EXPECT_EQ(bw.ports().size(), 1u);
    EXPECT_NE(bw.port("v"), nullptr);
}

TEST(BusNodeWidget, ConstructWithWires) {
    Node node;
    node.id = "bus1";
    node.name = "Main Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    std::vector<Wire> wires;
    wires.push_back(Wire::make("w1",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v")));
    wires.push_back(Wire::make("w2",
        wire_output("bus1", "v"),
        wire_input("load1", "v_in")));

    visual::BusNodeWidget bw(node, visual::BusOrientation::Horizontal, wires);

    // 2 alias ports (w1, w2) + 1 base "v" port = 3
    EXPECT_EQ(bw.ports().size(), 3u);
    EXPECT_NE(bw.port("w1"), nullptr);
    EXPECT_NE(bw.port("w2"), nullptr);
    EXPECT_NE(bw.port("v"), nullptr);
}

TEST(BusNodeWidget, ResolveWirePort) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    std::vector<Wire> wires;
    wires.push_back(Wire::make("w1",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v")));

    visual::BusNodeWidget bw(node, visual::BusOrientation::Horizontal, wires);

    // Asking for "v" with wire_id "w1" should return the alias port
    auto* alias = bw.resolveWirePort("v", "w1");
    ASSERT_NE(alias, nullptr);
    EXPECT_EQ(alias->name(), "w1");

    // Asking for "v" without wire_id should return the base "v" port
    auto* base = bw.resolveWirePort("v", "");
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->name(), "v");
}

TEST(BusNodeWidget, ConnectWire) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    visual::BusNodeWidget bw(node);
    EXPECT_EQ(bw.ports().size(), 1u);

    Wire wire = Wire::make("w1",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v"));
    bw.connectWire(wire);

    // Now 1 alias + 1 base = 2
    EXPECT_EQ(bw.ports().size(), 2u);
    EXPECT_NE(bw.port("w1"), nullptr);
}

TEST(BusNodeWidget, DisconnectWire) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    Wire w1 = Wire::make("w1",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v"));
    Wire w2 = Wire::make("w2",
        wire_output("bus1", "v"),
        wire_input("load1", "v_in"));

    std::vector<Wire> wires = {w1, w2};
    visual::BusNodeWidget bw(node, visual::BusOrientation::Horizontal, wires);

    EXPECT_EQ(bw.ports().size(), 3u);

    bw.disconnectWire(w1);

    // Now 1 alias (w2) + 1 base = 2
    EXPECT_EQ(bw.ports().size(), 2u);
    EXPECT_EQ(bw.port("w1"), nullptr);
    EXPECT_NE(bw.port("w2"), nullptr);
}

TEST(BusNodeWidget, SwapAliasPorts) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    Wire w1 = Wire::make("w1",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v"));
    Wire w2 = Wire::make("w2",
        wire_output("bus1", "v"),
        wire_input("load1", "v_in"));

    std::vector<Wire> wires = {w1, w2};
    visual::BusNodeWidget bw(node, visual::BusOrientation::Horizontal, wires);

    // Get positions before swap
    auto* p1_before = bw.port("w1");
    auto* p2_before = bw.port("w2");
    ASSERT_NE(p1_before, nullptr);
    ASSERT_NE(p2_before, nullptr);

    Pt pos1_before = p1_before->localPos();
    Pt pos2_before = p2_before->localPos();

    EXPECT_TRUE(bw.swapAliasPorts("w1", "w2"));

    // After swap, w1 should be at w2's old position and vice versa
    auto* p1_after = bw.port("w1");
    auto* p2_after = bw.port("w2");
    ASSERT_NE(p1_after, nullptr);
    ASSERT_NE(p2_after, nullptr);

    EXPECT_FLOAT_EQ(p1_after->localPos().x, pos2_before.x);
    EXPECT_FLOAT_EQ(p2_after->localPos().x, pos1_before.x);
}

TEST(BusNodeWidget, SwapNonexistentFails) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    visual::BusNodeWidget bw(node);

    EXPECT_FALSE(bw.swapAliasPorts("nope1", "nope2"));
}

TEST(BusNodeWidget, HorizontalOrientation) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";
    node.size_wh(160, 32);

    visual::BusNodeWidget bw(node, visual::BusOrientation::Horizontal);

    EXPECT_EQ(bw.orientation(), visual::BusOrientation::Horizontal);
}

TEST(BusNodeWidget, VerticalOrientation) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";
    node.size_wh(32, 160);

    visual::BusNodeWidget bw(node, visual::BusOrientation::Vertical);

    EXPECT_EQ(bw.orientation(), visual::BusOrientation::Vertical);
}

TEST(BusNodeWidget, IgnoresUnrelatedWires) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    std::vector<Wire> wires;
    // Wire NOT connected to bus1
    wires.push_back(Wire::make("w_other",
        wire_output("bat1", "v_out"),
        wire_input("load1", "v_in")));

    visual::BusNodeWidget bw(node, visual::BusOrientation::Horizontal, wires);

    // Only the base "v" port
    EXPECT_EQ(bw.ports().size(), 1u);
}

TEST(BusNodeWidget, ConnectUnrelatedWireIgnored) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    visual::BusNodeWidget bw(node);
    EXPECT_EQ(bw.ports().size(), 1u);

    // Wire not connected to bus1
    Wire wire = Wire::make("w_other",
        wire_output("bat1", "v_out"),
        wire_input("load1", "v_in"));
    bw.connectWire(wire);

    // Still just the base port
    EXPECT_EQ(bw.ports().size(), 1u);
}

// ============================================================================
// NodeFactory
// ============================================================================

TEST(NodeFactory, CreatesBusNode) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    auto w = visual::NodeFactory::create(node);
    ASSERT_NE(w, nullptr);
    EXPECT_NE(dynamic_cast<visual::BusNodeWidget*>(w.get()), nullptr);
}

TEST(NodeFactory, CreatesRefNode) {
    Node node;
    node.id = "gnd1";
    node.name = "GND";
    node.type_name = "RefNode";
    node.render_hint = "ref";

    auto w = visual::NodeFactory::create(node);
    ASSERT_NE(w, nullptr);
    EXPECT_NE(dynamic_cast<visual::RefNodeWidget*>(w.get()), nullptr);
}

TEST(NodeFactory, CreatesGroupNode) {
    Node node;
    node.id = "grp1";
    node.name = "Group";
    node.type_name = "Group";
    node.render_hint = "group";

    auto w = visual::NodeFactory::create(node);
    ASSERT_NE(w, nullptr);
    EXPECT_NE(dynamic_cast<visual::GroupNodeWidget*>(w.get()), nullptr);
}

TEST(NodeFactory, CreatesTextNode) {
    Node node;
    node.id = "txt1";
    node.name = "Text";
    node.type_name = "TextNode";
    node.render_hint = "text";

    auto w = visual::NodeFactory::create(node);
    ASSERT_NE(w, nullptr);
    EXPECT_NE(dynamic_cast<visual::TextNodeWidget*>(w.get()), nullptr);
}

TEST(NodeFactory, CreatesStandardNodeByDefault) {
    Node node;
    node.id = "bat1";
    node.name = "Battery";
    node.type_name = "Battery";
    node.input("v_in", PortType::V);
    node.output("v_out", PortType::V);

    auto w = visual::NodeFactory::create(node);
    ASSERT_NE(w, nullptr);
    EXPECT_NE(dynamic_cast<visual::NodeWidget*>(w.get()), nullptr);
}

TEST(NodeFactory, BusNodeWithWires) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    std::vector<Wire> wires;
    wires.push_back(Wire::make("w1",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v")));

    auto w = visual::NodeFactory::create(node, wires);
    auto* bus = dynamic_cast<visual::BusNodeWidget*>(w.get());
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->ports().size(), 2u);  // 1 alias + 1 base
}

// ============================================================================
// REGRESSION: setCustomColor via base Widget* pointer (all widget types)
// ============================================================================
// The color picker dialog needs to update any node widget type through a
// Widget* pointer returned by Scene::find(). Before the fix, setCustomColor
// was not virtual, so the call through Widget* was a no-op.

TEST(RefNodeWidget, SetCustomColorViaBasePointer) {
    Node node;
    node.id = "gnd1";
    node.name = "GND";
    node.type_name = "RefNode";
    node.render_hint = "ref";

    visual::RefNodeWidget rw(node);
    visual::Widget* base = &rw;

    EXPECT_FALSE(base->customColor().has_value());
    base->setCustomColor(0xFFAABBCC);
    EXPECT_TRUE(base->customColor().has_value());
    EXPECT_EQ(base->customColor().value(), 0xFFAABBCCu);

    base->setCustomColor(std::nullopt);
    EXPECT_FALSE(base->customColor().has_value());
}

TEST(TextNodeWidget, SetCustomColorViaBasePointer) {
    Node node;
    node.id = "txt1";
    node.name = "Note";
    node.type_name = "TextNode";
    node.render_hint = "text";

    visual::TextNodeWidget tw(node);
    visual::Widget* base = &tw;

    EXPECT_FALSE(base->customColor().has_value());
    base->setCustomColor(0xFF001122);
    EXPECT_TRUE(base->customColor().has_value());
    EXPECT_EQ(base->customColor().value(), 0xFF001122u);

    base->setCustomColor(std::nullopt);
    EXPECT_FALSE(base->customColor().has_value());
}

TEST(GroupNodeWidget, SetCustomColorViaBasePointer) {
    Node node;
    node.id = "grp1";
    node.name = "G";
    node.type_name = "Group";
    node.render_hint = "group";

    visual::GroupNodeWidget gw(node);
    visual::Widget* base = &gw;

    EXPECT_FALSE(base->customColor().has_value());
    base->setCustomColor(0xFF334455);
    EXPECT_TRUE(base->customColor().has_value());
    EXPECT_EQ(base->customColor().value(), 0xFF334455u);

    base->setCustomColor(std::nullopt);
    EXPECT_FALSE(base->customColor().has_value());
}

TEST(BusNodeWidget, SetCustomColorViaBasePointer) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    visual::BusNodeWidget bw(node);
    visual::Widget* base = &bw;

    EXPECT_FALSE(base->customColor().has_value());
    base->setCustomColor(0xFF667788);
    EXPECT_TRUE(base->customColor().has_value());
    EXPECT_EQ(base->customColor().value(), 0xFF667788u);

    base->setCustomColor(std::nullopt);
    EXPECT_FALSE(base->customColor().has_value());
}

// ============================================================================
// REGRESSION: renderPost exists on all specialized widgets
// ============================================================================
// Before the fix, selection borders could be overdrawn by child content.
// renderPost ensures selection overlays are always on top.

TEST(RefNodeWidget, RenderPostDoesNotCrash) {
    Node node;
    node.id = "gnd1";
    node.name = "GND";
    node.type_name = "RefNode";
    node.render_hint = "ref";

    visual::RefNodeWidget rw(node);
    visual::RenderContext ctx;
    ctx.zoom = 1.0f;
    ctx.pan = Pt(0, 0);
    rw.renderPost(nullptr, ctx);  // Should not crash
}

TEST(TextNodeWidget, RenderPostDoesNotCrash) {
    Node node;
    node.id = "txt1";
    node.name = "Note";
    node.type_name = "TextNode";
    node.render_hint = "text";

    visual::TextNodeWidget tw(node);
    visual::RenderContext ctx;
    ctx.zoom = 1.0f;
    ctx.pan = Pt(0, 0);
    tw.renderPost(nullptr, ctx);
}

TEST(GroupNodeWidget, RenderPostDoesNotCrash) {
    Node node;
    node.id = "grp1";
    node.name = "G";
    node.type_name = "Group";
    node.render_hint = "group";

    visual::GroupNodeWidget gw(node);
    visual::RenderContext ctx;
    ctx.zoom = 1.0f;
    ctx.pan = Pt(0, 0);
    gw.renderPost(nullptr, ctx);
}

TEST(BusNodeWidget, RenderPostDoesNotCrash) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    visual::BusNodeWidget bw(node);
    visual::RenderContext ctx;
    ctx.zoom = 1.0f;
    ctx.pan = Pt(0, 0);
    bw.renderPost(nullptr, ctx);
}

// ============================================================================
// REGRESSION: Bus disconnect preserves other wires
// ============================================================================
// Before the fix, disconnecting one wire from a bus could cause all other
// wires to vanish (cascading destruction via rebuildPorts -> Port::~Port
// -> WireEnd::~WireEnd -> Wire::remove_from_scene). The fix calls
// clearWire() before rebuilding ports.

TEST(BusNodeWidget, DisconnectOneWirePreservesOthers) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";

    Wire w1 = Wire::make("w1",
        wire_output("bat1", "v_out"),
        wire_input("bus1", "v"));
    Wire w2 = Wire::make("w2",
        wire_output("bus1", "v"),
        wire_input("load1", "v_in"));
    Wire w3 = Wire::make("w3",
        wire_output("gen1", "v_out"),
        wire_input("bus1", "v"));

    std::vector<Wire> wires = {w1, w2, w3};
    visual::BusNodeWidget bw(node, visual::BusOrientation::Horizontal, wires);

    // 3 alias ports + 1 base = 4
    EXPECT_EQ(bw.ports().size(), 4u);

    // Disconnect w1 — w2 and w3 should survive
    bw.disconnectWire(w1);

    EXPECT_EQ(bw.ports().size(), 3u);  // 2 alias + 1 base
    EXPECT_EQ(bw.port("w1"), nullptr);
    EXPECT_NE(bw.port("w2"), nullptr);
    EXPECT_NE(bw.port("w3"), nullptr);
    EXPECT_NE(bw.port("v"), nullptr);
}

// ============================================================================
// REGRESSION: NodeFactory creates widgets with color from Node data
// ============================================================================
// The color picker writes to Node.color. On scene rebuild, NodeFactory must
// create widgets that inherit the custom color from the data layer.

TEST(NodeFactory, CreatesWidgetWithCustomColor) {
    Node node;
    node.id = "bat1";
    node.name = "Battery";
    node.type_name = "Battery";
    node.color = NodeColor{0.8f, 0.2f, 0.1f, 1.0f};

    auto w = visual::NodeFactory::create(node);
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->customColor().has_value());
}

TEST(NodeFactory, BusNodeCreatedWithCustomColor) {
    Node node;
    node.id = "bus1";
    node.name = "Bus";
    node.type_name = "Bus";
    node.render_hint = "bus";
    node.color = NodeColor{0.1f, 0.9f, 0.5f, 1.0f};

    auto w = visual::NodeFactory::create(node);
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->customColor().has_value());
}

TEST(NodeFactory, GroupNodeCreatedWithCustomColor) {
    Node node;
    node.id = "grp1";
    node.name = "G";
    node.type_name = "Group";
    node.render_hint = "group";
    node.color = NodeColor{0.3f, 0.6f, 0.9f, 0.5f};

    auto w = visual::NodeFactory::create(node);
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->customColor().has_value());
}
