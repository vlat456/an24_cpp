#include "editor/visual/presentation/node_slot_layout.h"

#include <cassert>
#include <algorithm>

namespace editor::presentation {

namespace {

Rect inset_rect(const Rect& rect, float inset) {
    Rect result;
    result.x = rect.x + inset;
    result.y = rect.y + inset;
    result.w = std::max(0.0f, rect.w - inset * 2.0f);
    result.h = std::max(0.0f, rect.h - inset * 2.0f);
    return result;
}

void append_slot(std::vector<SlotAssignment>& slots, NodeSlot slot, const Rect& bounds) {
    slots.push_back(SlotAssignment{slot, bounds});
}

void place_fragment_node(const PresentationNode& node,
                         const Rect& bounds,
                         std::vector<FragmentPlacement>& placements) {
    placements.push_back(FragmentPlacement{node.element_id, bounds});

    if (node.children.empty()) {
        return;
    }

    assert(node.layout != LayoutKind::None && "LayoutKind::None cannot have children");

    if (node.layout == LayoutKind::Overlay) {
        for (const PresentationNode& child : node.children) {
            place_fragment_node(child, bounds, placements);
        }
        return;
    }

    const float gap = node.gap;
    const float child_count = static_cast<float>(node.children.size());
    if (node.layout == LayoutKind::Column) {
        const float total_gap = gap * std::max(0.0f, child_count - 1.0f);
        const float child_h = child_count > 0.0f ? std::max(0.0f, (bounds.h - total_gap) / child_count) : 0.0f;
        float cursor_y = bounds.y;
        for (const PresentationNode& child : node.children) {
            Rect child_bounds{bounds.x, cursor_y, bounds.w, child_h};
            place_fragment_node(child, child_bounds, placements);
            cursor_y += child_h + gap;
        }
        return;
    }

    const float total_gap = gap * std::max(0.0f, child_count - 1.0f);
    const float child_w = child_count > 0.0f ? std::max(0.0f, (bounds.w - total_gap) / child_count) : 0.0f;
    float cursor_x = bounds.x;
    for (const PresentationNode& child : node.children) {
        Rect child_bounds{cursor_x, bounds.y, child_w, bounds.h};
        place_fragment_node(child, child_bounds, placements);
        cursor_x += child_w + gap;
    }
}

} // namespace

NodeSlotLayout layout_node_presentation(const NodePresentation& presentation,
                                        ui::Pt node_size,
                                        const NodeSlotLayoutStyle& style) {
    NodeSlotLayout result;

    const float width = std::max(node_size.x, style.min_width);
    const float height = std::max(node_size.y, style.min_height);
    result.node_bounds = Rect{0.0f, 0.0f, width, height};

    const Rect header{0.0f, 0.0f, width, style.header_height};
    const float body_y = style.header_height + style.top_strip_height;
    const float body_h = std::max(0.0f, height - body_y - style.bottom_strip_height);
    const Rect body{0.0f, body_y, width, body_h};
    const Rect top_ports{0.0f, style.header_height, width, style.top_strip_height};
    const Rect bottom_ports{0.0f, height - style.bottom_strip_height, width, style.bottom_strip_height};
    const Rect left_ports{0.0f, body_y, style.side_strip_width, body_h};
    const Rect right_ports{width - style.side_strip_width, body_y, style.side_strip_width, body_h};
    const Rect body_content{
        left_ports.w,
        body_y,
        std::max(0.0f, width - left_ports.w - right_ports.w),
        body_h,
    };
    const Rect overlay = inset_rect(result.node_bounds, style.overlay_inset);

    append_slot(result.slots, NodeSlot::Header, header);
    append_slot(result.slots, NodeSlot::TopPorts, top_ports);
    append_slot(result.slots, NodeSlot::LeftPorts, left_ports);
    append_slot(result.slots, NodeSlot::Body, body_content);
    append_slot(result.slots, NodeSlot::RightPorts, right_ports);
    append_slot(result.slots, NodeSlot::BottomPorts, bottom_ports);
    append_slot(result.slots, NodeSlot::Overlay, overlay);

    const Rect content_bounds = inset_rect(body_content, style.body_padding);
    place_fragment_node(presentation.content.root, content_bounds, result.placements);
    return result;
}

} // namespace editor::presentation
