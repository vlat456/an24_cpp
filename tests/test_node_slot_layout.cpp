#include <gtest/gtest.h>

#include "editor/visual/presentation/node_presentation.h"
#include "editor/visual/presentation/node_slot_layout.h"

using namespace editor::presentation;

namespace {

PresentationNode make_column_fragment(const PresentationSpec& /*spec*/) {
    PresentationNode root;
    root.element_id = core::InternedId(1);
    root.layout = LayoutKind::Column;
    root.gap = 6.0f;

    PresentationNode top;
    top.element_id = core::InternedId(2);

    PresentationNode bottom;
    bottom.element_id = core::InternedId(3);

    root.children.push_back(std::move(top));
    root.children.push_back(std::move(bottom));
    return root;
}

NodePresentation make_presentation(core::InternedId type_id = core::InternedId(100)) {
    PresentationSpec spec;
    spec.node_id = core::InternedId(10);
    spec.type_id = type_id;
    spec.title = "Test Node";

    NodePresenterRegistry registry;
    registry.register_presenter(type_id, NodePresenter{NodeFrameKind::Standard, &make_column_fragment});
    NodePresentationCompileContext ctx{&registry};
    return compile_node_presentation(ctx, spec);
}

const ui::Rect* find_slot(const NodeSlotLayout& layout, NodeSlot slot) {
    for (const SlotAssignment& assignment : layout.slots) {
        if (assignment.slot == slot) {
            return &assignment.bounds;
        }
    }
    return nullptr;
}

const ui::Rect* find_placement(const NodeSlotLayout& layout, core::InternedId element_id) {
    for (const FragmentPlacement& placement : layout.placements) {
        if (placement.element_id == element_id) {
            return &placement.bounds;
        }
    }
    return nullptr;
}

/// Helper to build a PresentationSpec and compile with a custom presenter lambda.
template <typename Fn>
NodePresentation compile_with_presenter(core::InternedId node_id, core::InternedId type_id,
                                        const std::string& title, Fn presenter_fn) {
    PresentationSpec spec;
    spec.node_id = node_id;
    spec.type_id = type_id;
    spec.title = title;

    NodePresenterRegistry registry;
    registry.register_presenter(type_id, NodePresenter{NodeFrameKind::Standard, presenter_fn});
    return compile_node_presentation(NodePresentationCompileContext{&registry}, spec);
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

    const ui::Rect* root = find_placement(layout, core::InternedId(1));
    ASSERT_NE(root, nullptr);
    EXPECT_FLOAT_EQ(root->x, 28.0f);
    EXPECT_FLOAT_EQ(root->y, 32.0f);
    EXPECT_FLOAT_EQ(root->w, 124.0f);
    EXPECT_FLOAT_EQ(root->h, 80.0f);
}

TEST(NodeSlotLayoutTest, SplitsColumnChildrenEvenlyUsingGap) {
    NodePresentation presentation = make_presentation();
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    const ui::Rect* top = find_placement(layout, core::InternedId(2));
    const ui::Rect* bottom = find_placement(layout, core::InternedId(3));
    ASSERT_NE(top, nullptr);
    ASSERT_NE(bottom, nullptr);

    EXPECT_FLOAT_EQ(top->x, 28.0f);
    EXPECT_FLOAT_EQ(top->w, 124.0f);
    EXPECT_FLOAT_EQ(top->h, 37.0f);
    EXPECT_FLOAT_EQ(bottom->y, 75.0f);
    EXPECT_FLOAT_EQ(bottom->h, 37.0f);
}

TEST(NodeSlotLayoutTest, OverlayChildrenReuseParentBounds) {
    auto presentation = compile_with_presenter(
        core::InternedId(11), core::InternedId(200), "Overlay",
        +[](const PresentationSpec&) -> PresentationNode {
            PresentationNode root;
            root.element_id = core::InternedId(20);
            root.layout = LayoutKind::Overlay;
            PresentationNode a;
            a.element_id = core::InternedId(21);
            PresentationNode b;
            b.element_id = core::InternedId(22);
            root.children.push_back(std::move(a));
            root.children.push_back(std::move(b));
            return root;
        });
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    const ui::Rect* root = find_placement(layout, core::InternedId(20));
    const ui::Rect* a = find_placement(layout, core::InternedId(21));
    const ui::Rect* b = find_placement(layout, core::InternedId(22));
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
    auto presentation = compile_with_presenter(
        core::InternedId(30), core::InternedId(300), "CustomGap",
        +[](const PresentationSpec&) -> PresentationNode {
            PresentationNode root;
            root.element_id = core::InternedId(31);
            root.layout = LayoutKind::Column;
            root.gap = 10.0f;
            PresentationNode a;
            a.element_id = core::InternedId(32);
            PresentationNode b;
            b.element_id = core::InternedId(33);
            root.children.push_back(std::move(a));
            root.children.push_back(std::move(b));
            return root;
        });
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    const ui::Rect* a = find_placement(layout, core::InternedId(32));
    const ui::Rect* b = find_placement(layout, core::InternedId(33));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_FLOAT_EQ(a->h, 35.0f);
    EXPECT_FLOAT_EQ(b->y, a->y + a->h + 10.0f);
    EXPECT_FLOAT_EQ(b->h, 35.0f);
}

TEST(NodeSlotLayoutTest, ZeroGapNodePacksChildrenTightly) {
    auto presentation = compile_with_presenter(
        core::InternedId(40), core::InternedId(400), "NoGap",
        +[](const PresentationSpec&) -> PresentationNode {
            PresentationNode root;
            root.element_id = core::InternedId(41);
            root.layout = LayoutKind::Column;
            root.gap = 0.0f;
            PresentationNode a;
            a.element_id = core::InternedId(42);
            PresentationNode b;
            b.element_id = core::InternedId(43);
            root.children.push_back(std::move(a));
            root.children.push_back(std::move(b));
            return root;
        });
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    const ui::Rect* a = find_placement(layout, core::InternedId(42));
    const ui::Rect* b = find_placement(layout, core::InternedId(43));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_FLOAT_EQ(a->h, 40.0f);
    EXPECT_FLOAT_EQ(b->y, a->y + a->h);
    EXPECT_FLOAT_EQ(b->h, 40.0f);
}

TEST(NodeSlotLayoutTest, RowLayoutSplitsHorizontally) {
    auto presentation = compile_with_presenter(
        core::InternedId(50), core::InternedId(500), "Row",
        +[](const PresentationSpec&) -> PresentationNode {
            PresentationNode root;
            root.element_id = core::InternedId(51);
            root.layout = LayoutKind::Row;
            root.gap = 4.0f;
            PresentationNode a;
            a.element_id = core::InternedId(52);
            PresentationNode b;
            b.element_id = core::InternedId(53);
            root.children.push_back(std::move(a));
            root.children.push_back(std::move(b));
            return root;
        });
    NodeSlotLayout layout = layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f));

    const ui::Rect* a = find_placement(layout, core::InternedId(52));
    const ui::Rect* b = find_placement(layout, core::InternedId(53));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_FLOAT_EQ(a->w, 60.0f);
    EXPECT_FLOAT_EQ(b->x, a->x + a->w + 4.0f);
    EXPECT_FLOAT_EQ(b->w, 60.0f);
}

TEST(NodeSlotLayoutTest, NoneLayoutWithChildrenDiesInDebug) {
#ifndef NDEBUG
    auto presentation = compile_with_presenter(
        core::InternedId(60), core::InternedId(600), "Invalid",
        +[](const PresentationSpec&) -> PresentationNode {
            PresentationNode root;
            root.element_id = core::InternedId(61);
            root.layout = LayoutKind::None;
            PresentationNode child;
            child.element_id = core::InternedId(62);
            root.children.push_back(std::move(child));
            return root;
        });

    EXPECT_DEATH((void)layout_node_presentation(presentation, ui::Pt(180.0f, 120.0f)), "");
#endif
}

TEST(NodeSlotLayoutTest, SideRailRowSpacingMatchesMeasuredRowHeight) {
    NodeShellLayoutSpec spec;
    spec.header_height = 24.0f;
    spec.footer_height = 16.0f;
    spec.row_height = 16.0f;
    spec.port_diameter = 6.0f;
    spec.left_indent = 10.0f;
    spec.right_indent = 10.0f;
    spec.layout_grid = 16.0f;
    spec.min_gap = 0.0f;

    for (int i = 0; i < 3; ++i) {
        spec.left_entries.push_back(RailEntryMetrics{"port", 30.0f, 9.0f});
    }
    for (int i = 0; i < 2; ++i) {
        spec.right_entries.push_back(RailEntryMetrics{"port", 30.0f, 9.0f});
    }

    NodeShellLayout measured = measure_node_shell(spec);
    NodeShellLayout arranged = arrange_node_shell(spec, measured.preferred_size);

    EXPECT_GE(arranged.body.h, 3.0f * spec.row_height);

    ASSERT_EQ(arranged.left_rail.size(), 3u);
    for (size_t i = 0; i < arranged.left_rail.size(); ++i) {
        float expected_row_y = arranged.body.y + static_cast<float>(i) * spec.row_height;
        float expected_port_y = expected_row_y + (spec.row_height - spec.port_diameter) * 0.5f;
        EXPECT_FLOAT_EQ(arranged.left_rail[i].port_bounds.y, expected_port_y)
            << "Left port " << i << " y mismatch";
    }

    ASSERT_EQ(arranged.right_rail.size(), 2u);
    for (size_t i = 0; i < arranged.right_rail.size(); ++i) {
        float expected_row_y = arranged.body.y + static_cast<float>(i) * spec.row_height;
        float expected_port_y = expected_row_y + (spec.row_height - spec.port_diameter) * 0.5f;
        EXPECT_FLOAT_EQ(arranged.right_rail[i].port_bounds.y, expected_port_y)
            << "Right port " << i << " y mismatch";
    }

    float last_port_bottom = arranged.left_rail[2].port_bounds.y + arranged.left_rail[2].port_bounds.h;
    EXPECT_LE(last_port_bottom, arranged.body.y + arranged.body.h);
}
