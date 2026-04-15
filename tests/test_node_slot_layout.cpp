#include <gtest/gtest.h>

#include "editor/visual/presentation/node_presentation.h"
#include "editor/visual/presentation/node_slot_layout.h"

using namespace editor::presentation;

namespace {

PresentationNode make_column_fragment(const bp2::Blueprint::Node& /*node*/, ui::InternedId /*type_id*/) {
    PresentationNode root;
    root.element_id = ui::InternedId(1);
    root.layout = LayoutKind::Column;
    root.gap = 6.0f;

    PresentationNode top;
    top.element_id = ui::InternedId(2);

    PresentationNode bottom;
    bottom.element_id = ui::InternedId(3);

    root.children.push_back(std::move(top));
    root.children.push_back(std::move(bottom));
    return root;
}

NodePresentation make_presentation(ui::InternedId type_id = ui::InternedId(100)) {
    bp2::Blueprint::Node node;
    node.semantic.id = ui::InternedId(10);
    node.view.name = "Test Node";

    NodePresenterRegistry registry;
    registry.register_presenter(type_id, NodePresenter{NodeFrameKind::Standard, &make_column_fragment});
    NodePresentationCompileContext ctx{&registry};
    return compile_node_presentation(ctx, node, type_id);
}

const ui::Rect* find_slot(const NodeSlotLayout& layout, NodeSlot slot) {
    for (const SlotAssignment& assignment : layout.slots) {
        if (assignment.slot == slot) {
            return &assignment.bounds;
        }
    }
    return nullptr;
}

const ui::Rect* find_placement(const NodeSlotLayout& layout, ui::InternedId element_id) {
    for (const FragmentPlacement& placement : layout.placements) {
        if (placement.element_id == element_id) {
            return &placement.bounds;
        }
    }
    return nullptr;
}

} // namespace

TEST(NodeSlotLayoutTest, ComputesCanonicalShellSlots) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    const ui::Rect* header = find_slot(layout, NodeSlot::Header);
    const ui::Rect* body = find_slot(layout, NodeSlot::Body);
    const ui::Rect* left_ports = find_slot(layout, NodeSlot::LeftPorts);
    const ui::Rect* right_ports = find_slot(layout, NodeSlot::RightPorts);
    const ui::Rect* overlay = find_slot(layout, NodeSlot::Overlay);

    ASSERT_NE(header, nullptr);
    ASSERT_NE(body, nullptr);
    ASSERT_NE(left_ports, nullptr);
    ASSERT_NE(right_ports, nullptr);
    ASSERT_NE(overlay, nullptr);

    EXPECT_FLOAT_EQ(header->h, 24.0f);
    EXPECT_FLOAT_EQ(left_ports->w, 20.0f);
    EXPECT_FLOAT_EQ(right_ports->w, 20.0f);
    EXPECT_FLOAT_EQ(body->x, 20.0f);
    EXPECT_FLOAT_EQ(body->w, 140.0f);
    EXPECT_FLOAT_EQ(overlay->w, 180.0f);
    EXPECT_FLOAT_EQ(overlay->h, 120.0f);
}

TEST(NodeSlotLayoutTest, ClampsToMinimumNodeSize) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(10.0f, 10.0f));

    EXPECT_FLOAT_EQ(layout.node_bounds.w, 120.0f);
    EXPECT_FLOAT_EQ(layout.node_bounds.h, 80.0f);
}

TEST(NodeSlotLayoutTest, PlacesFragmentWithinBodySlotPadding) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    const ui::Rect* root = find_placement(layout, ui::InternedId(1));
    ASSERT_NE(root, nullptr);
    EXPECT_FLOAT_EQ(root->x, 28.0f);
    EXPECT_FLOAT_EQ(root->y, 32.0f);
    EXPECT_FLOAT_EQ(root->w, 124.0f);
    EXPECT_FLOAT_EQ(root->h, 80.0f);
}

TEST(NodeSlotLayoutTest, SplitsColumnChildrenEvenlyUsingGap) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    const ui::Rect* top = find_placement(layout, ui::InternedId(2));
    const ui::Rect* bottom = find_placement(layout, ui::InternedId(3));
    ASSERT_NE(top, nullptr);
    ASSERT_NE(bottom, nullptr);

    EXPECT_FLOAT_EQ(top->x, 28.0f);
    EXPECT_FLOAT_EQ(top->w, 124.0f);
    EXPECT_FLOAT_EQ(top->h, 37.0f);
    EXPECT_FLOAT_EQ(bottom->y, 75.0f);
    EXPECT_FLOAT_EQ(bottom->h, 37.0f);
}

TEST(NodeSlotLayoutTest, OverlayChildrenReuseParentBounds) {
    bp2::Blueprint::Node node;
    node.semantic.id = ui::InternedId(11);
    node.view.name = "Overlay";

    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(200), NodePresenter{NodeFrameKind::Standard,
        [](const bp2::Blueprint::Node&, ui::InternedId) {
            PresentationNode root;
            root.element_id = ui::InternedId(20);
            root.layout = LayoutKind::Overlay;
            PresentationNode a;
            a.element_id = ui::InternedId(21);
            PresentationNode b;
            b.element_id = ui::InternedId(22);
            root.children.push_back(std::move(a));
            root.children.push_back(std::move(b));
            return root;
        }});
    NodePresentation presentation = compile_node_presentation(NodePresentationCompileContext{&registry}, node, ui::InternedId(200));
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    const ui::Rect* root = find_placement(layout, ui::InternedId(20));
    const ui::Rect* a = find_placement(layout, ui::InternedId(21));
    const ui::Rect* b = find_placement(layout, ui::InternedId(22));
    ASSERT_NE(root, nullptr);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_FLOAT_EQ(a->x, root->x);
    EXPECT_FLOAT_EQ(a->y, root->y);
    EXPECT_FLOAT_EQ(a->w, root->w);
    EXPECT_FLOAT_EQ(a->h, root->h);
    EXPECT_FLOAT_EQ(b->x, root->x);
    EXPECT_FLOAT_EQ(b->y, root->y);
    EXPECT_FLOAT_EQ(b->w, root->w);
    EXPECT_FLOAT_EQ(b->h, root->h);
}

TEST(NodeSlotLayoutTest, PerNodeGapOverridesDefault) {
    // Regression: gap must come from PresentationNode.gap, not from a style default.
    bp2::Blueprint::Node node;
    node.semantic.id = ui::InternedId(30);
    node.view.name = "CustomGap";

    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(300), NodePresenter{NodeFrameKind::Standard,
        [](const bp2::Blueprint::Node&, ui::InternedId) {
            PresentationNode root;
            root.element_id = ui::InternedId(31);
            root.layout = LayoutKind::Column;
            root.gap = 10.0f; // Non-default gap
            PresentationNode a;
            a.element_id = ui::InternedId(32);
            PresentationNode b;
            b.element_id = ui::InternedId(33);
            root.children.push_back(std::move(a));
            root.children.push_back(std::move(b));
            return root;
        }});
    NodePresentation presentation = compile_node_presentation(
        NodePresentationCompileContext{&registry}, node, ui::InternedId(300));
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    const ui::Rect* a = find_placement(layout, ui::InternedId(32));
    const ui::Rect* b = find_placement(layout, ui::InternedId(33));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    // With gap=10, 2 children in 80h content: (80-10)/2 = 35 each
    // child_a at y=32, child_b at y=32+35+10=77
    EXPECT_FLOAT_EQ(a->h, 35.0f);
    EXPECT_FLOAT_EQ(b->y, a->y + a->h + 10.0f);
    EXPECT_FLOAT_EQ(b->h, 35.0f);
}

TEST(NodeSlotLayoutTest, ZeroGapNodePacksChildrenTightly) {
    // Regression: a node with gap=0 must produce zero spacing regardless of any style.
    bp2::Blueprint::Node node;
    node.semantic.id = ui::InternedId(40);
    node.view.name = "NoGap";

    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(400), NodePresenter{NodeFrameKind::Standard,
        [](const bp2::Blueprint::Node&, ui::InternedId) {
            PresentationNode root;
            root.element_id = ui::InternedId(41);
            root.layout = LayoutKind::Column;
            root.gap = 0.0f;
            PresentationNode a;
            a.element_id = ui::InternedId(42);
            PresentationNode b;
            b.element_id = ui::InternedId(43);
            root.children.push_back(std::move(a));
            root.children.push_back(std::move(b));
            return root;
        }});
    NodePresentation presentation = compile_node_presentation(
        NodePresentationCompileContext{&registry}, node, ui::InternedId(400));
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    const ui::Rect* a = find_placement(layout, ui::InternedId(42));
    const ui::Rect* b = find_placement(layout, ui::InternedId(43));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    // gap=0: 80h / 2 = 40 each, b starts immediately after a
    EXPECT_FLOAT_EQ(a->h, 40.0f);
    EXPECT_FLOAT_EQ(b->y, a->y + a->h);
    EXPECT_FLOAT_EQ(b->h, 40.0f);
}

TEST(NodeSlotLayoutTest, RowLayoutSplitsHorizontally) {
    bp2::Blueprint::Node node;
    node.semantic.id = ui::InternedId(50);
    node.view.name = "Row";

    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(500), NodePresenter{NodeFrameKind::Standard,
        [](const bp2::Blueprint::Node&, ui::InternedId) {
            PresentationNode root;
            root.element_id = ui::InternedId(51);
            root.layout = LayoutKind::Row;
            root.gap = 4.0f;
            PresentationNode a;
            a.element_id = ui::InternedId(52);
            PresentationNode b;
            b.element_id = ui::InternedId(53);
            root.children.push_back(std::move(a));
            root.children.push_back(std::move(b));
            return root;
        }});
    NodePresentation presentation = compile_node_presentation(
        NodePresentationCompileContext{&registry}, node, ui::InternedId(500));
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    const ui::Rect* a = find_placement(layout, ui::InternedId(52));
    const ui::Rect* b = find_placement(layout, ui::InternedId(53));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    // body_content: x=28, w=124 (after padding from 140)
    // Row with gap=4, 2 children: (124-4)/2 = 60 each
    EXPECT_FLOAT_EQ(a->w, 60.0f);
    EXPECT_FLOAT_EQ(b->x, a->x + a->w + 4.0f);
    EXPECT_FLOAT_EQ(b->w, 60.0f);
}

TEST(NodeSlotLayoutTest, NoneLayoutWithChildrenDiesInDebug) {
#ifndef NDEBUG
    bp2::Blueprint::Node node;
    node.semantic.id = ui::InternedId(60);
    node.view.name = "Invalid";

    NodePresenterRegistry registry;
    registry.register_presenter(ui::InternedId(600), NodePresenter{NodeFrameKind::Standard,
        [](const bp2::Blueprint::Node&, ui::InternedId) {
            PresentationNode root;
            root.element_id = ui::InternedId(61);
            root.layout = LayoutKind::None;
            PresentationNode child;
            child.element_id = ui::InternedId(62);
            root.children.push_back(std::move(child));
            return root;
        }});
    NodePresentation presentation = compile_node_presentation(
        NodePresentationCompileContext{&registry}, node, ui::InternedId(600));

    EXPECT_DEATH((void)layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f)), "");
#endif
}

TEST(NodeSlotLayoutTest, SideRailRowSpacingMatchesMeasuredRowHeight) {
    // Regression: arrange_side_rail() must use spec.row_height for row spacing,
    // not a hardcoded (port_diameter + 6.0f) expression. If they diverge, the
    // measure pass reserves N*row_height but arrange only steps N*(port_diameter+6),
    // leaving ports packed too tightly with wasted space at the bottom.
    NodeShellLayoutSpec spec;
    spec.header_height = 24.0f;
    spec.footer_height = 16.0f;
    spec.row_height = 16.0f;
    spec.port_diameter = 6.0f;
    spec.left_indent = 10.0f;
    spec.right_indent = 10.0f;
    spec.layout_grid = 16.0f;
    spec.min_gap = 0.0f;

    // 3 left ports, 2 right ports
    for (int i = 0; i < 3; ++i) {
        spec.left_entries.push_back(RailEntryMetrics{"port", 30.0f, 9.0f});
    }
    for (int i = 0; i < 2; ++i) {
        spec.right_entries.push_back(RailEntryMetrics{"port", 30.0f, 9.0f});
    }

    NodeShellLayout measured = measure_node_shell(spec);
    NodeShellLayout arranged = arrange_node_shell(spec, measured.preferred_size);

    // With 3 side rows, body should be at least 3 * row_height = 48
    EXPECT_GE(arranged.body.h, 3.0f * spec.row_height);

    // Left rail: 3 entries should be spaced exactly at row_height intervals
    ASSERT_EQ(arranged.left_rail.size(), 3u);
    for (size_t i = 0; i < arranged.left_rail.size(); ++i) {
        float expected_row_y = arranged.body.y + static_cast<float>(i) * spec.row_height;
        float expected_port_y = expected_row_y + (spec.row_height - spec.port_diameter) * 0.5f;
        EXPECT_FLOAT_EQ(arranged.left_rail[i].port_bounds.y, expected_port_y)
            << "Left port " << i << " y mismatch";
    }

    // Right rail: 2 entries should also be spaced at row_height intervals
    ASSERT_EQ(arranged.right_rail.size(), 2u);
    for (size_t i = 0; i < arranged.right_rail.size(); ++i) {
        float expected_row_y = arranged.body.y + static_cast<float>(i) * spec.row_height;
        float expected_port_y = expected_row_y + (spec.row_height - spec.port_diameter) * 0.5f;
        EXPECT_FLOAT_EQ(arranged.right_rail[i].port_bounds.y, expected_port_y)
            << "Right port " << i << " y mismatch";
    }

    // Verify the last port doesn't exceed the body region
    float last_port_bottom = arranged.left_rail[2].port_bounds.y + arranged.left_rail[2].port_bounds.h;
    EXPECT_LE(last_port_bottom, arranged.body.y + arranged.body.h);
}
