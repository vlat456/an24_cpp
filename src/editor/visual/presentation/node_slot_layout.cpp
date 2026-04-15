#include "editor/visual/presentation/node_slot_layout.h"

#include <cassert>
#include <algorithm>

namespace editor::presentation {

namespace {

ui::Rect inset_rect(const ui::Rect& rect, float inset) {
    ui::Rect result;
    result.x = rect.x + inset;
    result.y = rect.y + inset;
    result.w = std::max(0.0f, rect.w - inset * 2.0f);
    result.h = std::max(0.0f, rect.h - inset * 2.0f);
    return result;
}

void append_slot(std::vector<SlotAssignment>& slots, NodeSlot slot, const ui::Rect& bounds) {
    slots.push_back(SlotAssignment{slot, bounds});
}

void place_fragment_node(const PresentationNode& node,
                         const ui::Rect& bounds,
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
            ui::Rect child_bounds{bounds.x, cursor_y, bounds.w, child_h};
            place_fragment_node(child, child_bounds, placements);
            cursor_y += child_h + gap;
        }
        return;
    }

    const float total_gap = gap * std::max(0.0f, child_count - 1.0f);
    const float child_w = child_count > 0.0f ? std::max(0.0f, (bounds.w - total_gap) / child_count) : 0.0f;
    float cursor_x = bounds.x;
    for (const PresentationNode& child : node.children) {
        ui::Rect child_bounds{cursor_x, bounds.y, child_w, bounds.h};
        place_fragment_node(child, child_bounds, placements);
        cursor_x += child_w + gap;
    }
}

void arrange_side_rail(const std::vector<RailEntryMetrics>& entries,
                       bool is_left,
                       float node_w,
                       const ui::Rect& body,
                       float port_diameter,
                       float row_height,
                       float indent,
                       std::vector<RailPlacement>& out) {
    out.clear();
    out.reserve(entries.size());
    const float radius = port_diameter * 0.5f;
    for (size_t i = 0; i < entries.size(); ++i) {
        const float row_y = body.y + static_cast<float>(i) * row_height;
        const float port_x = is_left ? -radius : (node_w - radius);
        const float port_y = row_y + (row_height - port_diameter) * 0.5f;

        RailPlacement placement;
        placement.index = i;
        placement.port_bounds = ui::Rect{port_x, port_y, port_diameter, port_diameter};

        const float label_h = entries[i].label_height;
        const float label_y = row_y + (row_height - label_h) * 0.5f;
        if (is_left) {
            placement.label_bounds = ui::Rect{indent, label_y, std::max(0.0f, node_w - indent), label_h};
        } else {
            placement.label_bounds = ui::Rect{0.0f, label_y, std::max(0.0f, node_w - indent), label_h};
        }
        out.push_back(placement);
    }
}

void arrange_edge_rail(const std::vector<RailEntryMetrics>& entries,
                       bool is_top,
                       float node_w,
                       float node_h,
                       float layout_grid,
                       float port_diameter,
                       float label_offset,
                       std::vector<RailPlacement>& out) {
    out.clear();
    out.reserve(entries.size());
    const size_t n = entries.size();
    if (n == 0) return;
    const float radius = port_diameter * 0.5f;
    const float center_x = node_w * 0.5f;

    for (size_t i = 0; i < n; ++i) {
        float ideal_x = center_x + (static_cast<float>(i) - static_cast<float>(n - 1) * 0.5f) * layout_grid;
        float snapped_x = std::round(ideal_x / layout_grid) * layout_grid;

        RailPlacement placement;
        placement.index = i;
        placement.port_bounds = ui::Rect{
            snapped_x - radius,
            is_top ? -radius : (node_h - radius),
            port_diameter,
            port_diameter,
        };

        const float label_w = entries[i].label_width;
        const float label_h = entries[i].label_height;
        placement.label_bounds = ui::Rect{
            snapped_x - label_w * 0.5f,
            is_top ? (placement.port_bounds.y + port_diameter + label_offset)
                   : (placement.port_bounds.y - label_h - label_offset),
            label_w,
            label_h,
        };
        out.push_back(placement);
    }
}

} // namespace

NodeShellContentPolicy compile_node_shell_content_policy(bp2::NodeContentType content_type,
                                                         float content_margin_x,
                                                         float content_margin_y) {
    NodeShellContentPolicy policy;
    policy.margin_x = content_margin_x;
    policy.margin_y = content_margin_y;

    switch (content_type) {
        case bp2::NodeContentType::Switch:
            policy.preferred_size = ui::Pt(48.0f, 20.0f);
            break;
        case bp2::NodeContentType::VerticalToggle:
            policy.preferred_size = ui::Pt(16.0f, 48.0f);
            policy.reserve_width = false;
            policy.reserve_height = false;
            break;
        case bp2::NodeContentType::Slider:
            policy.preferred_size = ui::Pt(60.0f, 16.0f);
            break;
        case bp2::NodeContentType::Indicator:
            policy.preferred_size = ui::Pt(24.0f, 24.0f);
            policy.reserve_width = false;
            break;
        case bp2::NodeContentType::Knob:
            policy.preferred_size = ui::Pt(48.0f, 48.0f);
            break;
        case bp2::NodeContentType::Gauge:
            policy.preferred_size = ui::Pt(80.0f, 92.0f);
            break;
        case bp2::NodeContentType::None:
        default:
            break;
    }

    return policy;
}

NodeShellLayoutSpec compile_node_shell_layout_spec(
    float header_preferred_width,
    float footer_preferred_width,
    float port_diameter,
    float left_label_offset,
    float right_label_offset,
    float top_label_offset,
    float bottom_label_offset,
    float min_gap,
    float layout_grid,
    float row_height,
    const ResolvedLayout& resolved_layout,
    MeasureLabelFn measure_label,
    void* measure_user_data,
    const NodeShellContentPolicy& content_policy,
    float header_height,
    float footer_height) {
    NodeShellLayoutSpec spec;
    spec.header_height = header_height;
    spec.footer_height = footer_height;
    spec.header_preferred_width = header_preferred_width;
    spec.footer_preferred_width = footer_preferred_width;
    spec.left_indent = resolved_layout.left.empty() ? 0.0f : (port_diameter + left_label_offset);
    spec.right_indent = resolved_layout.right.empty() ? 0.0f : (port_diameter + right_label_offset);
    spec.min_gap = min_gap;
    spec.layout_grid = layout_grid;
    spec.row_height = row_height;
    spec.port_diameter = port_diameter;
    spec.top_label_offset = top_label_offset;
    spec.bottom_label_offset = bottom_label_offset;
    auto measure_side = [&](const std::vector<ResolvedPort>& ports,
                            std::vector<RailEntryMetrics>& out) {
        out.clear();
        out.reserve(ports.size());
        for (const auto& port : ports) {
            if (measure_label) {
                out.push_back(measure_label(port.port_name, measure_user_data));
            } else {
                RailEntryMetrics metrics;
                metrics.label_text = port.port_name;
                out.push_back(metrics);
            }
        }
    };

    measure_side(resolved_layout.left, spec.left_entries);
    measure_side(resolved_layout.right, spec.right_entries);
    measure_side(resolved_layout.top, spec.top_entries);
    measure_side(resolved_layout.bottom, spec.bottom_entries);
    spec.content_preferred_size = content_policy.preferred_size;
    spec.content_align_x = content_policy.align_x;
    spec.content_align_y = content_policy.align_y;
    spec.content_reserve_width = content_policy.reserve_width;
    spec.content_reserve_height = content_policy.reserve_height;
    spec.content_margin_x = content_policy.margin_x;
    spec.content_margin_y = content_policy.margin_y;
    return spec;
}

NodeShellLayout measure_node_shell(const NodeShellLayoutSpec& spec) {
    NodeShellLayout result;

    float left_labels_w = 0.0f;
    for (const auto& e : spec.left_entries) left_labels_w = std::max(left_labels_w, e.label_width);
    float right_labels_w = 0.0f;
    for (const auto& e : spec.right_entries) right_labels_w = std::max(right_labels_w, e.label_width);

    const bool has_left = !spec.left_entries.empty();
    const bool has_right = !spec.right_entries.empty();
    float width = std::max(spec.header_preferred_width, spec.footer_preferred_width);
    width = std::max(width, spec.left_indent + left_labels_w + (has_left && has_right ? spec.min_gap : 0.0f) + right_labels_w + spec.right_indent);

    if (spec.content_preferred_size.x > 0.0f && spec.content_reserve_width) {
        width = std::max(width, spec.content_preferred_size.x + spec.content_margin_x * 2.0f);
    }

    const size_t max_hstrip = std::max(spec.top_entries.size(), spec.bottom_entries.size());
    if (max_hstrip > 0) {
        width = std::max(width, static_cast<float>(max_hstrip + 1) * spec.layout_grid);
    }

    float side_rows_h = static_cast<float>(std::max(spec.left_entries.size(), spec.right_entries.size())) * spec.row_height;
    float content_h = 0.0f;
    if (spec.content_preferred_size.y > 0.0f) {
        if (spec.content_reserve_height) {
            content_h = spec.content_preferred_size.y + spec.content_margin_y * 2.0f;
        } else {
            side_rows_h = std::max(side_rows_h, spec.content_preferred_size.y + spec.content_margin_y * 2.0f);
        }
    }

    float height = spec.header_height
        + (!spec.top_entries.empty() ? spec.row_height : 0.0f)
        + side_rows_h
        + content_h
        + (!spec.bottom_entries.empty() ? spec.row_height : 0.0f)
        + spec.footer_height;

    result.preferred_size = ui::Pt{std::max(width, 120.0f), std::max(height, 80.0f)};
    result.minimum_size = ui::Pt{
        std::max(std::max(spec.left_indent + (has_left && has_right ? spec.min_gap : 0.0f) + spec.right_indent,
                          max_hstrip > 0 ? static_cast<float>(max_hstrip + 1) * spec.layout_grid : 0.0f),
                 16.0f),
        std::max(spec.header_height
                 + (!spec.top_entries.empty() ? spec.row_height : 0.0f)
                 + static_cast<float>(std::max(spec.left_entries.size(), spec.right_entries.size())) * spec.row_height
                 + (!spec.bottom_entries.empty() ? spec.row_height : 0.0f)
                 + spec.footer_height,
                 16.0f)
    };
    result.node_bounds = ui::Rect{0.0f, 0.0f, result.preferred_size.x, result.preferred_size.y};
    return result;
}

NodeShellLayout arrange_node_shell(const NodeShellLayoutSpec& spec, ui::Pt node_size) {
    NodeShellLayout result = measure_node_shell(spec);
    const float width = std::max(node_size.x, result.minimum_size.x);
    const float height = std::max(node_size.y, result.minimum_size.y);
    result.node_bounds = ui::Rect{0.0f, 0.0f, width, height};

    result.header = ui::Rect{0.0f, 0.0f, width, spec.header_height};
    result.footer = ui::Rect{0.0f, height - spec.footer_height, width, spec.footer_height};
    result.top_ports = ui::Rect{0.0f, spec.header_height, width, spec.top_entries.empty() ? 0.0f : spec.row_height};
    result.bottom_ports = ui::Rect{0.0f, result.footer.y - (spec.bottom_entries.empty() ? 0.0f : spec.row_height), width, spec.bottom_entries.empty() ? 0.0f : spec.row_height};

    const float body_y = result.top_ports.y + result.top_ports.h;
    const float body_h = std::max(0.0f, result.bottom_ports.y - body_y);
    result.body = ui::Rect{0.0f, body_y, width, body_h};

    float content_top = body_y + spec.content_margin_y;
    float content_h = std::max(0.0f, body_h - spec.content_margin_y * 2.0f);
    if (spec.content_reserve_height) {
        const float side_rows_h = static_cast<float>(std::max(spec.left_entries.size(), spec.right_entries.size())) * spec.row_height;
        content_top = body_y + side_rows_h + spec.content_margin_y;
        content_h = std::max(0.0f, body_h - side_rows_h - spec.content_margin_y * 2.0f);
    }
    result.content_bounds = ui::Rect{
        spec.content_margin_x + (std::max(0.0f, width - spec.content_margin_x * 2.0f - spec.content_preferred_size.x)) * spec.content_align_x,
        content_top + (std::max(0.0f, content_h - spec.content_preferred_size.y)) * spec.content_align_y,
        spec.content_preferred_size.x,
        spec.content_preferred_size.y,
    };

    arrange_side_rail(spec.left_entries, true, width, result.body, spec.port_diameter, spec.row_height, spec.left_indent, result.left_rail);
    arrange_side_rail(spec.right_entries, false, width, result.body, spec.port_diameter, spec.row_height, spec.right_indent, result.right_rail);
    arrange_edge_rail(spec.top_entries, true, width, height, spec.layout_grid, spec.port_diameter, spec.top_label_offset, result.top_rail);
    arrange_edge_rail(spec.bottom_entries, false, width, height, spec.layout_grid, spec.port_diameter, spec.bottom_label_offset, result.bottom_rail);

    return result;
}

NodeSlotLayout layout_node_presentation(const NodePresentation& presentation,
                                        ui::Pt node_size,
                                        const NodeSlotLayoutStyle& style) {
    NodeSlotLayout result;

    // Activate footer only when a type label is present
    const float effective_footer_height = presentation.shell.type_name.empty()
        ? 0.0f
        : std::max(style.footer_height, 16.0f);

    const float width = std::max(node_size.x, style.min_width);
    const float height = std::max(node_size.y, style.min_height);
    result.node_bounds = ui::Rect{0.0f, 0.0f, width, height};

    const ui::Rect header{0.0f, 0.0f, width, style.header_height};
    const float body_y = style.header_height + style.top_strip_height;
    const float footer_y = std::max(body_y, height - effective_footer_height);
    const ui::Rect footer{0.0f, footer_y, width, effective_footer_height};
    const float body_h = std::max(0.0f, footer_y - body_y - style.bottom_strip_height);
    const ui::Rect body{0.0f, body_y, width, body_h};
    const ui::Rect top_ports{0.0f, style.header_height, width, style.top_strip_height};
    const ui::Rect bottom_ports{0.0f, footer_y - style.bottom_strip_height, width, style.bottom_strip_height};
    const ui::Rect left_ports{0.0f, body_y, style.side_strip_width, body_h};
    const ui::Rect right_ports{width - style.side_strip_width, body_y, style.side_strip_width, body_h};
    const ui::Rect body_content{
        left_ports.w,
        body_y,
        std::max(0.0f, width - left_ports.w - right_ports.w),
        body_h,
    };
    const ui::Rect overlay = inset_rect(result.node_bounds, style.overlay_inset);

    append_slot(result.slots, NodeSlot::Header, header);
    append_slot(result.slots, NodeSlot::TopPorts, top_ports);
    append_slot(result.slots, NodeSlot::LeftPorts, left_ports);
    append_slot(result.slots, NodeSlot::Body, body_content);
    append_slot(result.slots, NodeSlot::RightPorts, right_ports);
    append_slot(result.slots, NodeSlot::BottomPorts, bottom_ports);
    append_slot(result.slots, NodeSlot::Footer, footer);
    append_slot(result.slots, NodeSlot::Overlay, overlay);

    const ui::Rect content_bounds = inset_rect(body_content, style.body_padding);
    place_fragment_node(presentation.content, content_bounds, result.placements);
    return result;
}

} // namespace editor::presentation
