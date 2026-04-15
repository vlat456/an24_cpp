#pragma once

#include "editor/visual/presentation/node_presentation.h"
#include "visual/node/port_layout_resolver.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/math/pt.h"
#include "ui/math/rect.h"
#include <vector>

namespace editor::presentation {

enum class NodeSlot {
    Header,
    Body,
    Footer,
    LeftPorts,
    RightPorts,
    TopPorts,
    BottomPorts,
    Overlay,
};

struct SlotAssignment {
    NodeSlot slot = NodeSlot::Body;
    ui::Rect bounds;
};

struct FragmentPlacement {
    ui::InternedId element_id;
    ui::Rect bounds;
};

struct NodeSlotLayout {
    ui::Rect node_bounds;
    std::vector<SlotAssignment> slots;
    std::vector<FragmentPlacement> placements;
};

struct NodeSlotLayoutStyle {
    float min_width = 120.0f;
    float min_height = 80.0f;
    float header_height = 24.0f;
    float footer_height = 0.0f;     ///< 0 = no footer; set >0 when type_name is present
    float side_strip_width = 20.0f;
    float top_strip_height = 0.0f;
    float bottom_strip_height = 0.0f;
    float body_padding = 8.0f;
    float overlay_inset = 0.0f;
};

struct RailEntryMetrics {
    std::string_view label_text;
    float label_width = 0.0f;
    float label_height = 0.0f;
};

struct RailPlacement {
    size_t index = 0;
    ui::Rect port_bounds;
    ui::Rect label_bounds;
};

struct NodeShellLayoutSpec {
    float header_height = 24.0f;
    float footer_height = 16.0f;
    float header_preferred_width = 0.0f;
    float footer_preferred_width = 0.0f;

    float left_indent = 0.0f;
    float right_indent = 0.0f;
    float min_gap = 0.0f;
    float layout_grid = 16.0f;
    float row_height = 16.0f;
    float port_diameter = 10.0f;
    float top_label_offset = 0.0f;
    float bottom_label_offset = 0.0f;

    std::vector<RailEntryMetrics> left_entries;
    std::vector<RailEntryMetrics> right_entries;
    std::vector<RailEntryMetrics> top_entries;
    std::vector<RailEntryMetrics> bottom_entries;

    ui::Pt content_preferred_size{};
    float content_align_x = 0.5f;
    float content_align_y = 0.5f;
    bool content_reserve_width = true;
    bool content_reserve_height = true;
    float content_margin_x = 0.0f;
    float content_margin_y = 0.0f;
};

struct NodeShellContentPolicy {
    ui::Pt preferred_size{};
    float align_x = 0.5f;
    float align_y = 0.5f;
    bool reserve_width = true;
    bool reserve_height = true;
    float margin_x = 0.0f;
    float margin_y = 0.0f;
};

NodeShellContentPolicy compile_node_shell_content_policy(bp2::NodeContentType content_type,
                                                         float content_margin_x,
                                                         float content_margin_y);

struct NodeShellLayout {
    ui::Rect node_bounds;
    ui::Rect header;
    ui::Rect top_ports;
    ui::Rect body;
    ui::Rect bottom_ports;
    ui::Rect footer;
    ui::Rect content_bounds;
    ui::Pt preferred_size{};
    ui::Pt minimum_size{};
    std::vector<RailPlacement> left_rail;
    std::vector<RailPlacement> right_rail;
    std::vector<RailPlacement> top_rail;
    std::vector<RailPlacement> bottom_rail;
};

using MeasureLabelFn = RailEntryMetrics (*)(std::string_view text, void* user_data);

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
    float header_height = 24.0f,
    float footer_height = 16.0f);

NodeShellLayout measure_node_shell(const NodeShellLayoutSpec& spec);
NodeShellLayout arrange_node_shell(const NodeShellLayoutSpec& spec, ui::Pt node_size);

NodeSlotLayout layout_node_presentation(const NodePresentation& presentation,
                                        ui::Pt node_size,
                                        const NodeSlotLayoutStyle& style = {});

} // namespace editor::presentation
