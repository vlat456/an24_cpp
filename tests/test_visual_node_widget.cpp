#include <gtest/gtest.h>
#include "visual/node/visual_node.h"
#include "visual/scene.h"
#include "data/node.h"

// ============================================================================
// Basic construction
// ============================================================================

TEST(VisualNodeWidget, ConstructFromSimpleNode) {
    Node node;
    node.id = "bat1";
    node.name = "Battery";
    node.type_name = "Battery";
    node.input("v_in", PortType::V);
    node.output("v_out", PortType::V);

    visual::NodeWidget nw(node);

    EXPECT_EQ(nw.nodeId(), "bat1");
    EXPECT_EQ(nw.name(), "Battery");
    EXPECT_EQ(nw.typeName(), "Battery");
    EXPECT_TRUE(nw.isClickable());
}

TEST(VisualNodeWidget, HasCorrectPorts) {
    Node node;
    node.id = "n1";
    node.name = "TestNode";
    node.type_name = "Test";
    node.input("a", PortType::V);
    node.input("b", PortType::I);
    node.output("c", PortType::V);

    visual::NodeWidget nw(node);

    EXPECT_EQ(nw.ports().size(), 3u);
    EXPECT_NE(nw.port("a"), nullptr);
    EXPECT_NE(nw.port("b"), nullptr);
    EXPECT_NE(nw.port("c"), nullptr);
    EXPECT_EQ(nw.port("nonexistent"), nullptr);

    EXPECT_EQ(nw.port("a")->side(), PortSide::Input);
    EXPECT_EQ(nw.port("a")->type(), PortType::V);
    EXPECT_EQ(nw.port("b")->type(), PortType::I);
    EXPECT_EQ(nw.port("c")->side(), PortSide::Output);
}

TEST(VisualNodeWidget, AutoSizesAboveMinimum) {
    Node node;
    node.id = "n1";
    node.name = "X";
    node.type_name = "T";

    visual::NodeWidget nw(node);

    // Should be at least MIN_NODE_WIDTH (80) and snapped to grid (16)
    EXPECT_GE(nw.size().x, 80.0f);
    EXPECT_GT(nw.size().y, 0.0f);

    // Should be snapped to PORT_LAYOUT_GRID (16)
    float grid = 16.0f;
    EXPECT_FLOAT_EQ(std::fmod(nw.size().x, grid), 0.0f);
    EXPECT_FLOAT_EQ(std::fmod(nw.size().y, grid), 0.0f);
}

TEST(VisualNodeWidget, ExplicitSizeRespected) {
    Node node;
    node.id = "n1";
    node.name = "X";
    node.type_name = "T";
    node.size_wh(200, 160);

    visual::NodeWidget nw(node);

    // Explicit size should be respected if >= preferred
    EXPECT_GE(nw.size().x, 200.0f);
    EXPECT_GE(nw.size().y, 160.0f);
}

TEST(VisualNodeWidget, PositionFromNodeData) {
    Node node;
    node.id = "n1";
    node.name = "X";
    node.type_name = "T";
    node.at(100, 200);

    visual::NodeWidget nw(node);

    EXPECT_FLOAT_EQ(nw.localPos().x, 100.0f);
    EXPECT_FLOAT_EQ(nw.localPos().y, 200.0f);
}

// ============================================================================
// Content types
// ============================================================================

TEST(VisualNodeWidget, SwitchContent) {
    Node node;
    node.id = "sw1";
    node.name = "Switch";
    node.type_name = "Switch";
    node.input("v_in", PortType::V);
    node.output("v_out", PortType::V);

    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = true;
    content.tripped = false;
    node.with_content(content);

    visual::NodeWidget nw(node);

    EXPECT_EQ(nw.ports().size(), 2u);
    EXPECT_GT(nw.size().x, 0.0f);
    EXPECT_GT(nw.size().y, 0.0f);
}

TEST(VisualNodeWidget, GaugeContent) {
    Node node;
    node.id = "g1";
    node.name = "Voltmeter";
    node.type_name = "Voltmeter";
    node.input("v_in", PortType::V);

    NodeContent content;
    content.type = NodeContentType::Gauge;
    content.value = 12.5f;
    content.min = 0.0f;
    content.max = 30.0f;
    content.unit = "V";
    node.with_content(content);

    visual::NodeWidget nw(node);

    EXPECT_EQ(nw.ports().size(), 1u);
    EXPECT_GT(nw.size().y, 40.0f); // Gauge needs more vertical space
}

TEST(VisualNodeWidget, VerticalToggleContent) {
    Node node;
    node.id = "azs1";
    node.name = "AZS";
    node.type_name = "AZS";
    node.input("v_in", PortType::V);
    node.output("v_out", PortType::V);

    NodeContent content;
    content.type = NodeContentType::VerticalToggle;
    content.state = false;
    content.tripped = false;
    node.with_content(content);

    visual::NodeWidget nw(node);

    EXPECT_EQ(nw.ports().size(), 2u);
    EXPECT_NE(nw.port("v_in"), nullptr);
    EXPECT_NE(nw.port("v_out"), nullptr);
}

// ============================================================================
// Custom color
// ============================================================================

TEST(VisualNodeWidget, CustomColorFromNodeData) {
    Node node;
    node.id = "n1";
    node.name = "X";
    node.type_name = "T";
    node.color = NodeColor{0.5f, 0.3f, 0.1f, 1.0f};

    visual::NodeWidget nw(node);

    EXPECT_TRUE(nw.customColor().has_value());
}

TEST(VisualNodeWidget, NoCustomColorByDefault) {
    Node node;
    node.id = "n1";
    node.name = "X";
    node.type_name = "T";

    visual::NodeWidget nw(node);

    EXPECT_FALSE(nw.customColor().has_value());
}

// ============================================================================
// Scene integration
// ============================================================================

TEST(VisualNodeWidget, AddToScene) {
    visual::Scene scene;

    Node node;
    node.id = "bat1";
    node.name = "Battery";
    node.type_name = "Battery";
    node.input("v_in", PortType::V);
    node.output("v_out", PortType::V);
    node.at(50, 50);

    auto nw_ptr = std::make_unique<visual::NodeWidget>(node);
    auto* nw = nw_ptr.get();
    scene.add(std::move(nw_ptr));

    // Node should be findable by ID
    auto* found = scene.find("bat1");
    EXPECT_EQ(found, nw);

    // Ports should be clickable and in the grid
    EXPECT_TRUE(nw->port("v_in")->isClickable());
    EXPECT_TRUE(nw->port("v_out")->isClickable());
}

TEST(VisualNodeWidget, PortWorldPositions) {
    Node node;
    node.id = "n1";
    node.name = "Test";
    node.type_name = "T";
    node.input("in1", PortType::V);
    node.output("out1", PortType::V);
    node.at(100, 100);

    visual::NodeWidget nw(node);

    // Input port should be on the left side
    auto* in_port = nw.port("in1");
    ASSERT_NE(in_port, nullptr);
    Pt in_world = in_port->worldPos();

    // Output port should be on the right side
    auto* out_port = nw.port("out1");
    ASSERT_NE(out_port, nullptr);
    Pt out_world = out_port->worldPos();

    // Input port x should be less than output port x
    EXPECT_LT(in_world.x, out_world.x);

    // Both ports should be below the header (y > node.y)
    EXPECT_GT(in_world.y, 100.0f);
    EXPECT_GT(out_world.y, 100.0f);
}

// ============================================================================
// Update content
// ============================================================================

TEST(VisualNodeWidget, UpdateContentDoesNotCrash) {
    Node node;
    node.id = "sw1";
    node.name = "Switch";
    node.type_name = "Switch";
    node.input("v_in", PortType::V);
    node.output("v_out", PortType::V);

    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = false;
    node.with_content(content);

    visual::NodeWidget nw(node);

    // Update to new state - should not crash
    NodeContent updated;
    updated.type = NodeContentType::Switch;
    updated.state = true;
    nw.updateContent(updated);
}

// ============================================================================
// No ports node
// ============================================================================

TEST(VisualNodeWidget, NodeWithNoPorts) {
    Node node;
    node.id = "text1";
    node.name = "Label";
    node.type_name = "TextNode";

    visual::NodeWidget nw(node);

    EXPECT_EQ(nw.ports().size(), 0u);
    EXPECT_GT(nw.size().x, 0.0f);
    EXPECT_GT(nw.size().y, 0.0f);
}

// ============================================================================
// Asymmetric ports
// ============================================================================

TEST(VisualNodeWidget, AsymmetricPortCounts) {
    Node node;
    node.id = "n1";
    node.name = "Splitter";
    node.type_name = "Splitter";
    node.input("in", PortType::V);
    node.output("out1", PortType::V);
    node.output("out2", PortType::V);
    node.output("out3", PortType::V);

    visual::NodeWidget nw(node);

    EXPECT_EQ(nw.ports().size(), 4u);
    EXPECT_NE(nw.port("in"), nullptr);
    EXPECT_NE(nw.port("out1"), nullptr);
    EXPECT_NE(nw.port("out2"), nullptr);
    EXPECT_NE(nw.port("out3"), nullptr);
}

// ============================================================================
// REGRESSION: setCustomColor via base Widget pointer
// ============================================================================
// Before the fix, setCustomColor was not virtual on Widget. Calling it through
// a Widget* pointer would not dispatch to the concrete subclass, making it
// impossible for generic code (e.g., the color picker dialog) to update the
// visual color of any node type without dynamic_cast.

TEST(VisualNodeWidget, SetCustomColorViaBasePointer) {
    Node node;
    node.id = "n1";
    node.name = "X";
    node.type_name = "T";

    visual::NodeWidget nw(node);
    visual::Widget* base = &nw;

    EXPECT_FALSE(base->customColor().has_value());

    base->setCustomColor(0xFF112233);
    EXPECT_TRUE(base->customColor().has_value());
    EXPECT_EQ(base->customColor().value(), 0xFF112233u);

    // Reset via base pointer
    base->setCustomColor(std::nullopt);
    EXPECT_FALSE(base->customColor().has_value());
}

// ============================================================================
// REGRESSION: contentBounds for content selection fix
// ============================================================================
// Before the fix, clicks anywhere on a Switch/VerticalToggle node body would
// toggle the switch, preventing selection/dragging. The fix checks
// contentBounds() to only trigger toggles inside the content area.

TEST(VisualNodeWidget, ContentBoundsNonZeroForSwitch) {
    Node node;
    node.id = "sw1";
    node.name = "Switch";
    node.type_name = "Switch";
    node.input("v_in", PortType::V);
    node.output("v_out", PortType::V);

    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = false;
    content.tripped = false;
    node.with_content(content);

    visual::NodeWidget nw(node);

    Bounds cb = nw.contentBounds();
    // Content area should have non-zero width and height
    EXPECT_GT(cb.w, 0.0f);
    EXPECT_GT(cb.h, 0.0f);
}

TEST(VisualNodeWidget, ContentBoundsNonZeroForVerticalToggle) {
    Node node;
    node.id = "azs1";
    node.name = "AZS";
    node.type_name = "AZS";
    node.input("v_in", PortType::V);
    node.output("v_out", PortType::V);

    NodeContent content;
    content.type = NodeContentType::VerticalToggle;
    content.state = false;
    content.tripped = false;
    node.with_content(content);

    visual::NodeWidget nw(node);

    Bounds cb = nw.contentBounds();
    EXPECT_GT(cb.w, 0.0f);
    EXPECT_GT(cb.h, 0.0f);
}

TEST(VisualNodeWidget, ContentBoundsZeroWhenNoContent) {
    Node node;
    node.id = "n1";
    node.name = "Plain";
    node.type_name = "Battery";
    node.input("v_in", PortType::V);
    node.output("v_out", PortType::V);

    visual::NodeWidget nw(node);

    Bounds cb = nw.contentBounds();
    EXPECT_FLOAT_EQ(cb.w, 0.0f);
    EXPECT_FLOAT_EQ(cb.h, 0.0f);
}

TEST(VisualNodeWidget, ContentBoundsInsideNodeBounds) {
    Node node;
    node.id = "sw1";
    node.name = "Switch";
    node.type_name = "Switch";
    node.input("v_in", PortType::V);
    node.output("v_out", PortType::V);
    node.at(50, 50);

    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = false;
    content.tripped = false;
    node.with_content(content);

    visual::NodeWidget nw(node);

    Bounds cb = nw.contentBounds();
    // Content bounds are in node-local coordinates
    EXPECT_GE(cb.x, 0.0f);
    EXPECT_GE(cb.y, 0.0f);
    EXPECT_LE(cb.x + cb.w, nw.size().x);
    EXPECT_LE(cb.y + cb.h, nw.size().y);
}

TEST(VisualNodeWidget, ContentBoundsHeaderClickOutsideContent) {
    // Simulates the fix: a click in the header area should NOT be inside
    // contentBounds, allowing selection/dragging instead of toggle.
    Node node;
    node.id = "sw1";
    node.name = "Switch";
    node.type_name = "Switch";
    node.input("v_in", PortType::V);
    node.output("v_out", PortType::V);
    node.at(0, 0);

    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = false;
    content.tripped = false;
    node.with_content(content);

    visual::NodeWidget nw(node);

    Bounds cb = nw.contentBounds();
    // Header is at the top of the node. A click at (node_width/2, 5) should
    // be above the content area.
    EXPECT_FALSE(cb.contains(nw.size().x / 2.0f, 5.0f));
}

// ============================================================================
// REGRESSION: renderPost exists and guards against nullptr IDrawList
// ============================================================================
// Before the fix, selection borders were drawn in render(), which meant
// child content (header, ports) could overdraw the selection highlight.
// renderPost ensures the border is drawn after all children.

TEST(VisualNodeWidget, RenderPostDoesNotCrashWithNullDrawList) {
    Node node;
    node.id = "n1";
    node.name = "X";
    node.type_name = "T";

    visual::NodeWidget nw(node);

    visual::RenderContext ctx;
    ctx.zoom = 1.0f;
    ctx.pan = Pt(0, 0);
    // nullptr drawlist should be handled gracefully (early return)
    nw.renderPost(nullptr, ctx);
}

TEST(VisualNodeWidget, RenderDoesNotCrashWithNullDrawList) {
    Node node;
    node.id = "n1";
    node.name = "X";
    node.type_name = "T";

    visual::NodeWidget nw(node);

    visual::RenderContext ctx;
    ctx.zoom = 1.0f;
    ctx.pan = Pt(0, 0);
    nw.render(nullptr, ctx);
}
