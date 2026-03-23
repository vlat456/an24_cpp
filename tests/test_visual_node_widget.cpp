#include "ui/math/pt.h"

using ui::Pt;

#include <gtest/gtest.h>
#include "visual/node/visual_node.h"
#include "visual/node/node_factory.h"
#include "visual/port/visual_port.h"
#include "visual/primitives/primitives.h"
#include "visual/scene.h"
#include "editor/layout_constants.h"
#include "data/node.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"

static ui::StringInterner g_interner;

static bp2::Blueprint::Node to_bp2_node(const Node& node) {
    return visual::NodeFactory::to_bp2_node(node, g_interner);
}

// ============================================================================
// Basic construction
// ============================================================================

TEST(VisualNodeWidget, ConstructFromSimpleNode) {
    Node node;
    node.id = g_interner.intern("bat1");
    node.name = "Battery";
    node.type_name = "Battery";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    EXPECT_EQ(nw.nodeId(), "bat1");
    EXPECT_EQ(nw.name(), "Battery");
    EXPECT_EQ(nw.typeName(), "Battery");
    EXPECT_TRUE(nw.isClickable());
}

TEST(VisualNodeWidget, HasCorrectPorts) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "TestNode";
    node.type_name = "Test";
    node.input(g_interner.intern("a"), PortType::V);
    node.input(g_interner.intern("b"), PortType::I);
    node.output(g_interner.intern("c"), PortType::V);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

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

TEST(VisualNodeWidget, AutoSizesSnappedToGrid) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "X";
    node.type_name = "T";

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    // Size should be positive and snapped to PORT_LAYOUT_GRID (16)
    EXPECT_GT(nw.size().x, 0.0f);
    EXPECT_GT(nw.size().y, 0.0f);

    float grid = 16.0f;
    EXPECT_FLOAT_EQ(std::fmod(nw.size().x, grid), 0.0f);
    EXPECT_FLOAT_EQ(std::fmod(nw.size().y, grid), 0.0f);
}

TEST(VisualNodeWidget, ExplicitSizeRespected) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "X";
    node.type_name = "T";
    node.size_wh(200, 160);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    // Explicit size should be respected if >= preferred
    EXPECT_GE(nw.size().x, 200.0f);
    EXPECT_GE(nw.size().y, 160.0f);
}

TEST(VisualNodeWidget, PositionFromNodeData) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "X";
    node.type_name = "T";
    node.at(100, 200);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    EXPECT_FLOAT_EQ(nw.localPos().x, 100.0f);
    EXPECT_FLOAT_EQ(nw.localPos().y, 200.0f);
}

// ============================================================================
// Content types
// ============================================================================

TEST(VisualNodeWidget, SwitchContent) {
    Node node;
    node.id = g_interner.intern("sw1");
    node.name = "Switch";
    node.type_name = "Switch";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);

    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = true;
    content.tripped = false;
    node.with_content(content);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    EXPECT_EQ(nw.ports().size(), 2u);
    EXPECT_GT(nw.size().x, 0.0f);
    EXPECT_GT(nw.size().y, 0.0f);
}

TEST(VisualNodeWidget, GaugeContent) {
    Node node;
    node.id = g_interner.intern("g1");
    node.name = "Voltmeter";
    node.type_name = "Voltmeter";
    node.input(g_interner.intern("v_in"), PortType::V);

    NodeContent content;
    content.type = NodeContentType::Gauge;
    content.value = 12.5f;
    content.min = 0.0f;
    content.max = 30.0f;
    content.unit = "V";
    node.with_content(content);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    EXPECT_EQ(nw.ports().size(), 1u);
    EXPECT_GT(nw.size().y, 40.0f); // Gauge needs more vertical space
}

TEST(VisualNodeWidget, VerticalToggleContent) {
    Node node;
    node.id = g_interner.intern("azs1");
    node.name = "AZS";
    node.type_name = "AZS";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);

    NodeContent content;
    content.type = NodeContentType::VerticalToggle;
    content.state = false;
    content.tripped = false;
    node.with_content(content);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    EXPECT_EQ(nw.ports().size(), 2u);
    EXPECT_NE(nw.port("v_in"), nullptr);
    EXPECT_NE(nw.port("v_out"), nullptr);
}

// ============================================================================
// Custom color
// ============================================================================

TEST(VisualNodeWidget, CustomColorFromNodeData) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "X";
    node.type_name = "T";
    node.color = NodeColor{0.5f, 0.3f, 0.1f, 1.0f};

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    EXPECT_TRUE(nw.customColor().has_value());
}

TEST(VisualNodeWidget, NoCustomColorByDefault) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "X";
    node.type_name = "T";

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    EXPECT_FALSE(nw.customColor().has_value());
}

// ============================================================================
// Scene integration
// ============================================================================

TEST(VisualNodeWidget, AddToScene) {
    visual::Scene scene;

    Node node;
    node.id = g_interner.intern("bat1");
    node.name = "Battery";
    node.type_name = "Battery";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);
    node.at(50, 50);

    auto nw_ptr = std::make_unique<visual::NodeWidget>(to_bp2_node(node), g_interner);
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
    node.id = g_interner.intern("n1");
    node.name = "Test";
    node.type_name = "T";
    node.input(g_interner.intern("in1"), PortType::V);
    node.output(g_interner.intern("out1"), PortType::V);
    node.at(100, 100);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

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
    node.id = g_interner.intern("sw1");
    node.name = "Switch";
    node.type_name = "Switch";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);

    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = false;
    node.with_content(content);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

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
    node.id = g_interner.intern("text1");
    node.name = "Label";
    node.type_name = "TextNode";

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    EXPECT_EQ(nw.ports().size(), 0u);
    EXPECT_GT(nw.size().x, 0.0f);
    EXPECT_GT(nw.size().y, 0.0f);
}

// ============================================================================
// Asymmetric ports
// ============================================================================

TEST(VisualNodeWidget, AsymmetricPortCounts) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "Splitter";
    node.type_name = "Splitter";
    node.input(g_interner.intern("in"), PortType::V);
    node.output(g_interner.intern("out1"), PortType::V);
    node.output(g_interner.intern("out2"), PortType::V);
    node.output(g_interner.intern("out3"), PortType::V);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

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
    node.id = g_interner.intern("n1");
    node.name = "X";
    node.type_name = "T";

    visual::NodeWidget nw(to_bp2_node(node), g_interner);
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
    node.id = g_interner.intern("sw1");
    node.name = "Switch";
    node.type_name = "Switch";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);

    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = false;
    content.tripped = false;
    node.with_content(content);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    Bounds cb = nw.contentBounds();
    // Content area should have non-zero width and height
    EXPECT_GT(cb.w, 0.0f);
    EXPECT_GT(cb.h, 0.0f);
}

TEST(VisualNodeWidget, ContentBoundsNonZeroForVerticalToggle) {
    Node node;
    node.id = g_interner.intern("azs1");
    node.name = "AZS";
    node.type_name = "AZS";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);

    NodeContent content;
    content.type = NodeContentType::VerticalToggle;
    content.state = false;
    content.tripped = false;
    node.with_content(content);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    Bounds cb = nw.contentBounds();
    EXPECT_GT(cb.w, 0.0f);
    EXPECT_GT(cb.h, 0.0f);
}

TEST(VisualNodeWidget, ContentBoundsZeroWhenNoContent) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "Plain";
    node.type_name = "Battery";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    Bounds cb = nw.contentBounds();
    EXPECT_FLOAT_EQ(cb.w, 0.0f);
    EXPECT_FLOAT_EQ(cb.h, 0.0f);
}

TEST(VisualNodeWidget, ContentBoundsInsideNodeBounds) {
    Node node;
    node.id = g_interner.intern("sw1");
    node.name = "Switch";
    node.type_name = "Switch";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);
    node.at(50, 50);

    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = false;
    content.tripped = false;
    node.with_content(content);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

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
    node.id = g_interner.intern("sw1");
    node.name = "Switch";
    node.type_name = "Switch";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);
    node.at(0, 0);

    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = false;
    content.tripped = false;
    node.with_content(content);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

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
    node.id = g_interner.intern("n1");
    node.name = "X";
    node.type_name = "T";

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    visual::RenderContext ctx;
    ctx.zoom = 1.0f;
    ctx.pan = Pt(0, 0);
    // nullptr drawlist should be handled gracefully (early return)
    nw.renderPost(nullptr, ctx);
}

TEST(VisualNodeWidget, RenderDoesNotCrashWithNullDrawList) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "X";
    node.type_name = "T";

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    visual::RenderContext ctx;
    ctx.zoom = 1.0f;
    ctx.pan = Pt(0, 0);
    nw.render(nullptr, ctx);
}

// ============================================================================
// Regression: port placement at node edges (bug: ports not centered on edge)
// ============================================================================

TEST(VisualNodeWidget, InputPortCenterAtLeftEdge) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "Test";
    node.type_name = "T";
    node.input(g_interner.intern("in1"), PortType::V);
    node.output(g_interner.intern("out1"), PortType::V);
    node.at(100, 200);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    auto* in_port = nw.port("in1");
    ASSERT_NE(in_port, nullptr);

    // Port circle center = worldPos + (RADIUS, RADIUS)
    float center_x = in_port->worldPos().x + visual::PortConstants::RADIUS;
    // Must be at node's left edge
    EXPECT_FLOAT_EQ(center_x, nw.worldPos().x);
}

TEST(VisualNodeWidget, OutputPortCenterAtRightEdge) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "Test";
    node.type_name = "T";
    node.input(g_interner.intern("in1"), PortType::V);
    node.output(g_interner.intern("out1"), PortType::V);
    node.at(100, 200);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    auto* out_port = nw.port("out1");
    ASSERT_NE(out_port, nullptr);

    float center_x = out_port->worldPos().x + visual::PortConstants::RADIUS;
    // Must be at node's right edge
    EXPECT_FLOAT_EQ(center_x, nw.worldPos().x + nw.size().x);
}

TEST(VisualNodeWidget, MultiplePortsAllAtEdges) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "Multi";
    node.type_name = "T";
    node.input(g_interner.intern("a"), PortType::V);
    node.input(g_interner.intern("b"), PortType::I);
    node.output(g_interner.intern("c"), PortType::V);
    node.output(g_interner.intern("d"), PortType::Bool);
    node.at(50, 50);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    float left_edge = nw.worldPos().x;
    float right_edge = nw.worldPos().x + nw.size().x;

    for (auto* p : nw.ports()) {
        float cx = p->worldPos().x + visual::PortConstants::RADIUS;
        if (p->side() == PortSide::Input) {
            EXPECT_FLOAT_EQ(cx, left_edge)
                << "Input port '" << p->name() << "' center not at left edge";
        } else {
            EXPECT_FLOAT_EQ(cx, right_edge)
                << "Output port '" << p->name() << "' center not at right edge";
        }
    }
}

TEST(VisualNodeWidget, PortRowsHavePaddingBelowHeader) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "Test";
    node.type_name = "T";
    node.input(g_interner.intern("in1"), PortType::V);
    node.output(g_interner.intern("out1"), PortType::V);
    node.at(0, 0);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    // Header height is 24, port row should have vertical padding
    // Port is vertically centered in its parent container:
    //   container_y = header_h
    //   port_local_y = (ROW_HEIGHT - PORT_RADIUS*2) / 2
    //   port_world_y = header_h + port_local_y
    constexpr float header_h = 24.0f;
    constexpr float port_v_offset = (visual::PortConstants::ROW_HEIGHT
                                     - visual::PortConstants::RADIUS * 2) / 2.0f;

    auto* in_port = nw.port("in1");
    ASSERT_NE(in_port, nullptr);

    // Port top-left y should be at header + vertical centering offset
    float port_y = in_port->worldPos().y;
    EXPECT_GT(port_y, header_h)
        << "Port should be below header with padding, not flush";
    EXPECT_NEAR(port_y, header_h + port_v_offset, 1.0f)
        << "Port should be vertically centered in row below header";
}

TEST(VisualNodeWidget, VerticalTogglePortsAtEdges) {
    Node node;
    node.id = g_interner.intern("azs1");
    node.name = "AZS";
    node.type_name = "AZS";
    node.input(g_interner.intern("control"), PortType::Bool);
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);
    node.output(g_interner.intern("tripped"), PortType::Bool);
    node.at(100, 100);

    NodeContent content;
    content.type = NodeContentType::VerticalToggle;
    content.state = false;
    node.with_content(content);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    float left_edge = nw.worldPos().x;
    float right_edge = nw.worldPos().x + nw.size().x;

    for (auto* p : nw.ports()) {
        float cx = p->worldPos().x + visual::PortConstants::RADIUS;
        if (p->side() == PortSide::Input) {
            EXPECT_FLOAT_EQ(cx, left_edge)
                << "Input port '" << p->name() << "' not at left edge";
        } else {
            EXPECT_FLOAT_EQ(cx, right_edge)
                << "Output port '" << p->name() << "' not at right edge";
        }
    }
}

// Regression: right-column output labels in vertical toggle layout must be
// right-aligned. Before the fix, buildPortInColumn() created a Row with just
// a Label (no Spacer), so the label was left-aligned in the column.
TEST(VisualNodeWidget, VerticalToggleOutputLabelsRightAligned) {
    Node node;
    node.id = g_interner.intern("azs1");
    node.name = "AZS";
    node.type_name = "AZS";
    node.input(g_interner.intern("control"), PortType::Bool);
    node.output(g_interner.intern("v_out"), PortType::V);
    node.output(g_interner.intern("tripped"), PortType::Bool);
    node.at(100, 100);

    NodeContent content;
    content.type = NodeContentType::VerticalToggle;
    content.state = false;
    node.with_content(content);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    float node_right = nw.worldPos().x + nw.size().x;
    float indent = visual::PortConstants::RADIUS * 2 + visual::PortConstants::RIGHT_LABEL_OFFSET;

    // For each output port label, its right edge (worldPos.x + size.x) should be
    // close to the node right edge minus the port indent.
    float expected_right = node_right - indent;

    // Collect labels by walking widget tree
    std::function<void(const visual::Widget&)> visit;
    int right_labels_found = 0;
    visit = [&](const visual::Widget& w) {
        if (auto* label = dynamic_cast<const visual::Label*>(&w)) {
            float label_right = label->worldPos().x + label->size().x;
            float node_center = nw.worldPos().x + nw.size().x / 2.0f;
            if (label->worldPos().x > node_center) {
                // Right-column label — its right edge should be flush
                EXPECT_NEAR(label_right, expected_right, 1.0f)
                    << "Right-column label right edge should be flush with column edge";
                right_labels_found++;
            }
        }
        for (const auto& child : w.children()) {
            visit(static_cast<const visual::Widget&>(*child));
        }
    };
    visit(nw);

    EXPECT_EQ(right_labels_found, 2) << "Should find 2 output labels (v_out, tripped)";
}

// ============================================================================
// Four-sided layout: port override to opposite geometric side
// ============================================================================

TEST(VisualNodeWidget, OverriddenInputPortSnapsToRightEdge) {
    // An Input port overridden to the Right geometric side should have its
    // circle center at the node's right edge, not the left.
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "Override";
    node.type_name = "T";
    node.input(g_interner.intern("in1"), PortType::V);
    node.output(g_interner.intern("out1"), PortType::V);
    node.at(100, 100);

    // Override: move "in1" from Left → Right
    PortLayoutOverride ov;
    ov.port_name = "in1";
    ov.side = PortLayoutSide::Right;
    node.layout_overrides.push_back(ov);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    auto* in_port = nw.port("in1");
    ASSERT_NE(in_port, nullptr);

    // Logical side is still Input
    EXPECT_EQ(in_port->side(), PortSide::Input);
    // But geometric side is now Right
    EXPECT_EQ(in_port->layoutSide(), PortLayoutSide::Right);

    // Port center should be at node's right edge
    float center_x = in_port->worldPos().x + visual::PortConstants::RADIUS;
    float right_edge = nw.worldPos().x + nw.size().x;
    EXPECT_FLOAT_EQ(center_x, right_edge)
        << "Input port overridden to Right should snap to right edge";
}

TEST(VisualNodeWidget, OverriddenOutputPortSnapsToLeftEdge) {
    // An Output port overridden to the Left geometric side should have its
    // circle center at the node's left edge.
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "Override";
    node.type_name = "T";
    node.input(g_interner.intern("in1"), PortType::V);
    node.output(g_interner.intern("out1"), PortType::V);
    node.at(50, 50);

    // Override: move "out1" from Right → Left
    PortLayoutOverride ov;
    ov.port_name = "out1";
    ov.side = PortLayoutSide::Left;
    node.layout_overrides.push_back(ov);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    auto* out_port = nw.port("out1");
    ASSERT_NE(out_port, nullptr);

    // Logical side stays Output
    EXPECT_EQ(out_port->side(), PortSide::Output);
    // Geometric side is now Left
    EXPECT_EQ(out_port->layoutSide(), PortLayoutSide::Left);

    float center_x = out_port->worldPos().x + visual::PortConstants::RADIUS;
    float left_edge = nw.worldPos().x;
    EXPECT_FLOAT_EQ(center_x, left_edge)
        << "Output port overridden to Left should snap to left edge";
}

TEST(VisualNodeWidget, OverriddenPortToTopSnapsToTopEdge) {
    // A port overridden to the Top side should have its circle center at
    // the node's top edge.
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "TopOverride";
    node.type_name = "T";
    node.input(g_interner.intern("in1"), PortType::V);
    node.input(g_interner.intern("in2"), PortType::Bool);
    node.output(g_interner.intern("out1"), PortType::V);
    node.at(0, 0);

    // Override: move "in2" to Top
    PortLayoutOverride ov;
    ov.port_name = "in2";
    ov.side = PortLayoutSide::Top;
    node.layout_overrides.push_back(ov);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    auto* in2_port = nw.port("in2");
    ASSERT_NE(in2_port, nullptr);
    EXPECT_EQ(in2_port->layoutSide(), PortLayoutSide::Top);

    // Port center Y should be at node's top edge
    float center_y = in2_port->worldPos().y + visual::PortConstants::RADIUS;
    float top_edge = nw.worldPos().y;
    EXPECT_FLOAT_EQ(center_y, top_edge)
        << "Port overridden to Top should snap to top edge";
}

TEST(VisualNodeWidget, OverriddenPortToBottomSnapsToBottomEdge) {
    Node node;
    node.id = g_interner.intern("n1");
    node.name = "BotOverride";
    node.type_name = "T";
    node.input(g_interner.intern("in1"), PortType::V);
    node.output(g_interner.intern("out1"), PortType::V);
    node.output(g_interner.intern("out2"), PortType::Bool);
    node.at(0, 0);

    // Override: move "out2" to Bottom
    PortLayoutOverride ov;
    ov.port_name = "out2";
    ov.side = PortLayoutSide::Bottom;
    node.layout_overrides.push_back(ov);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    auto* out2_port = nw.port("out2");
    ASSERT_NE(out2_port, nullptr);
    EXPECT_EQ(out2_port->layoutSide(), PortLayoutSide::Bottom);

    float center_y = out2_port->worldPos().y + visual::PortConstants::RADIUS;
    float bottom_edge = nw.worldPos().y + nw.size().y;
    EXPECT_FLOAT_EQ(center_y, bottom_edge)
        << "Port overridden to Bottom should snap to bottom edge";
}

// ============================================================================
// Four-sided layout: content placement with overrides
// ============================================================================

TEST(VisualNodeWidget, FourSidedSwitchContentHasNonZeroBounds) {
    // When a Switch node has layout overrides (triggering four-sided layout),
    // the content widget should still be present and have non-zero bounds.
    Node node;
    node.id = g_interner.intern("sw1");
    node.name = "Switch";
    node.type_name = "Switch";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);
    node.at(0, 0);

    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = false;
    content.tripped = false;
    node.with_content(content);

    // Add an override to trigger four-sided layout
    PortLayoutOverride ov;
    ov.port_name = "v_in";
    ov.side = PortLayoutSide::Left;
    ov.position = 0;
    node.layout_overrides.push_back(ov);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    Bounds cb = nw.contentBounds();
    EXPECT_GT(cb.w, 0.0f) << "Switch content width should be non-zero in four-sided layout";
    EXPECT_GT(cb.h, 0.0f) << "Switch content height should be non-zero in four-sided layout";
}

TEST(VisualNodeWidget, FourSidedVerticalToggleContentHasNonZeroBounds) {
    // VerticalToggle with overrides falls back from special layout to
    // buildStandardLayout → buildFourSidedLayout. Content must still work.
    Node node;
    node.id = g_interner.intern("azs1");
    node.name = "AZS";
    node.type_name = "AZS";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);
    node.at(0, 0);

    NodeContent content;
    content.type = NodeContentType::VerticalToggle;
    content.state = false;
    content.tripped = false;
    node.with_content(content);

    // Override forces four-sided layout
    PortLayoutOverride ov;
    ov.port_name = "v_out";
    ov.side = PortLayoutSide::Bottom;
    node.layout_overrides.push_back(ov);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    Bounds cb = nw.contentBounds();
    EXPECT_GT(cb.w, 0.0f) << "VerticalToggle content width should be non-zero in four-sided layout";
    EXPECT_GT(cb.h, 0.0f) << "VerticalToggle content height should be non-zero in four-sided layout";
}

TEST(VisualNodeWidget, FourSidedContentBoundsInsideNode) {
    // Content bounds should remain inside the node even with overrides.
    Node node;
    node.id = g_interner.intern("sw1");
    node.name = "Switch";
    node.type_name = "Switch";
    node.input(g_interner.intern("v_in"), PortType::V);
    node.output(g_interner.intern("v_out"), PortType::V);
    node.at(50, 50);

    NodeContent content;
    content.type = NodeContentType::Switch;
    content.state = true;
    content.tripped = false;
    node.with_content(content);

    PortLayoutOverride ov;
    ov.port_name = "v_in";
    ov.side = PortLayoutSide::Top;
    node.layout_overrides.push_back(ov);

    visual::NodeWidget nw(to_bp2_node(node), g_interner);

    Bounds cb = nw.contentBounds();
    EXPECT_GE(cb.x, 0.0f);
    EXPECT_GE(cb.y, 0.0f);
    EXPECT_LE(cb.x + cb.w, nw.size().x);
    EXPECT_LE(cb.y + cb.h, nw.size().y);
}

// ============================================================================
// VerticalToggle: more ports increase node height
// ============================================================================

TEST(VisualNodeWidget, MorePortsIncreasesVerticalToggleHeight) {
    // Adding many more ports to a VerticalToggle layout should make the node
    // taller, since port rows stack vertically alongside the toggle.
    // The toggle widget is ~50px tall, so we need enough ports to exceed that.
    Node small_node;
    small_node.id = g_interner.intern("azs_s");
    small_node.name = "AZS";
    small_node.type_name = "AZS";
    small_node.input(g_interner.intern("v_in"), PortType::V);
    small_node.output(g_interner.intern("v_out"), PortType::V);

    NodeContent content;
    content.type = NodeContentType::VerticalToggle;
    content.state = false;

    small_node.with_content(content);
    visual::NodeWidget nw_small(small_node, g_interner);

    // Big node: 6 inputs so port column is ~96px (6 * 16), well above toggle height
    Node big_node;
    big_node.id = g_interner.intern("azs_b");
    big_node.name = "AZS";
    big_node.type_name = "AZS";
    big_node.input(g_interner.intern("i1"), PortType::V);
    big_node.input(g_interner.intern("i2"), PortType::V);
    big_node.input(g_interner.intern("i3"), PortType::V);
    big_node.input(g_interner.intern("i4"), PortType::Bool);
    big_node.input(g_interner.intern("i5"), PortType::Bool);
    big_node.input(g_interner.intern("i6"), PortType::Bool);
    big_node.output(g_interner.intern("v_out"), PortType::V);

    big_node.with_content(content);
    visual::NodeWidget nw_big(big_node, g_interner);

    EXPECT_GT(nw_big.size().y, nw_small.size().y)
        << "Node with 6 input port rows should be taller than node with 1";
}
