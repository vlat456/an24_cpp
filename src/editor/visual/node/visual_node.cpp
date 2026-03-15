#include "visual_node.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/handle_renderer.h"
#include "visual/render_context.h"
#include "editor/layout_constants.h"
#include "visual/node/bounds.h"
#include "visual/snap.h"
#include "data/node.h"
#include <algorithm>
#include <cmath>

namespace visual {

// ============================================================================
// Construction
// ============================================================================

NodeWidget::NodeWidget(const ::Node& data, const ui::StringInterner& interner)
    : node_iid_(data.id)
    , interner_(&interner)
    , name_(data.name)
    , type_name_(data.type_name)
{
    if (data.color.has_value()) {
        custom_fill_ = data.color->to_uint32();
    }

    setLocalPos(data.pos);
    buildLayout(data, interner);

    // Auto-size: compute preferred, apply minimum, snap to grid
    Pt preferred = layout_->preferredSize(nullptr);
    preferred.x = std::max(preferred.x, editor_constants::MIN_NODE_WIDTH);

    float w = preferred.x;
    float h = preferred.y;

    if (data.size_explicitly_set) {
        if (data.size.x >= preferred.x) w = data.size.x;
        if (data.size.y >= preferred.y) h = data.size.y;
    }

    // Snap to layout grid (round up to nearest PORT_LAYOUT_GRID)
    Pt snapped = editor_math::snap_size_to_layout_grid(Pt(w, h));
    w = snapped.x;
    h = snapped.y;

    layout(w, h);
}

// ============================================================================
// Layout construction
// ============================================================================

void NodeWidget::buildLayout(const ::Node& data, const ui::StringInterner& interner) {
    layout_ = emplaceChild<Column>();

    // -- Header --
    layout_->emplaceChild<HeaderWidget>(
        name_, render_theme::COLOR_HEADER_FILL, editor_constants::NODE_ROUNDING);

    // -- Port rows / Content --
    // VerticalToggle uses special layout, but falls back to standard when overrides present
    if (data.node_content.type == NodeContentType::VerticalToggle && data.layout_overrides.empty()) {
        buildVerticalToggleLayout(data, interner);
    } else {
        buildStandardLayout(data, interner);
    }

    // -- Type name footer --
    layout_->emplaceChild<TypeNameWidget>(type_name_);
}

void NodeWidget::buildStandardLayout(const ::Node& data, const ui::StringInterner& interner) {
    // Fast path: no overrides — use existing paired-row layout
    if (data.layout_overrides.empty()) {
        // Port rows: pair inputs and outputs
        size_t max_ports = std::max(data.inputs.size(), data.outputs.size());
        for (size_t i = 0; i < max_ports; i++) {
            std::string_view left_name;
            std::string_view right_name;
            if (i < data.inputs.size()) {
                left_name = interner.resolve(data.inputs[i].name);
            }
            PortType left_type = (i < data.inputs.size()) ? data.inputs[i].type : PortType::Any;
            if (i < data.outputs.size()) {
                right_name = interner.resolve(data.outputs[i].name);
            }
            PortType right_type = (i < data.outputs.size()) ? data.outputs[i].type : PortType::Any;
            buildPortRow(left_name, left_type, right_name, right_type);
        }
    } else {
        // Slow path: four-sided layout with overrides
        buildFourSidedLayout(data, interner);
    }

    // Content area
    if (data.node_content.type == NodeContentType::Gauge) {
        content_widget_ = layout_->emplaceChild<VoltmeterWidget>(
            data.node_content.value, data.node_content.min,
            data.node_content.max, data.node_content.unit);
    } else if (data.node_content.type == NodeContentType::Switch) {
        float margin = editor_constants::PORT_RADIUS + editor_constants::PORT_LABEL_GAP;
        float v_pad = 2.0f;
        auto* container = layout_->emplaceChild<Container>(
            Edges{margin, v_pad, margin, v_pad});
        container->setFlexible(true);
        content_widget_ = container->emplaceChild<SwitchWidget>(
            data.node_content.state, data.node_content.tripped);
    } else if (data.node_content.type == NodeContentType::VerticalToggle) {
        float margin = editor_constants::PORT_RADIUS + editor_constants::PORT_LABEL_GAP;
        auto* container = layout_->emplaceChild<Container>(
            Edges{margin, 5.0f, margin, 5.0f});
        container->setFlexible(true);
        content_widget_ = container->emplaceChild<VerticalToggleWidget>(
            data.node_content.state, data.node_content.tripped);
    } else if (data.node_content.type != NodeContentType::None) {
        float margin = editor_constants::PORT_RADIUS + editor_constants::PORT_LABEL_GAP;
        auto* container = layout_->emplaceChild<Container>(
            Edges{margin, 0, margin, 0});
        container->setFlexible(true);
        if (!data.node_content.label.empty()) {
            content_widget_ = container->emplaceChild<Label>(
                data.node_content.label, 10.0f, (uint32_t)0x00000000);
        } else {
            content_widget_ = container->emplaceChild<Spacer>();
        }
    }
}

void NodeWidget::buildVerticalToggleLayout(const ::Node& data, const ui::StringInterner& interner) {
    auto* main_row = layout_->emplaceChild<Row>();
    main_row->setFlexible(true);

    // Left column (input ports)
    auto* left_col = main_row->emplaceChild<Column>();
    for (const auto& p : data.inputs) {
        std::string_view name_sv = interner.resolve(p.name);
        buildPortInColumn(left_col, name_sv, p.type, true);
    }

    // Center column (vertical toggle)
    auto* center_col = main_row->emplaceChild<Column>();
    center_col->setFlexible(true);
    auto* toggle_container = center_col->emplaceChild<Container>(
        Edges{0, 5.0f, 0, 5.0f});
    toggle_container->setFlexible(true);
    content_widget_ = toggle_container->emplaceChild<VerticalToggleWidget>(
        data.node_content.state, data.node_content.tripped);

    // Right column (output ports)
    auto* right_col = main_row->emplaceChild<Column>();
    for (const auto& p : data.outputs) {
        std::string_view name_sv = interner.resolve(p.name);
        buildPortInColumn(right_col, name_sv, p.type, false);
    }
}

void NodeWidget::buildPortRow(std::string_view left_name, PortType left_type,
                              std::string_view right_name, PortType right_type) {
    using namespace editor_constants;

    // Build port row: [Label? Spacer Label?] inside padded container.
    // Port widgets are added as extra children of row_container (outside Row flow)
    // and positioned by post-layout snap in layout().
    constexpr float v_pad = (PORT_ROW_HEIGHT - PORT_LABEL_FONT_SIZE) / 2.0f;
    constexpr float label_indent = PORT_RADIUS * 2 + PORT_LABEL_GAP;
    auto* row_container = layout_->emplaceChild<Container>(
        Edges{label_indent, v_pad, label_indent, v_pad});
    auto* row = row_container->emplaceChild<Row>();

    // Left label (input)
    if (!left_name.empty()) {
        row->emplaceChild<Label>(left_name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR);
    }

    // Flexible spacer
    if (!left_name.empty() && !right_name.empty()) {
        auto* gap = row->emplaceChild<Container>(
            Edges{PORT_MIN_GAP / 2.0f, 0, PORT_MIN_GAP / 2.0f, 0});
        gap->setFlexible(true);
        gap->emplaceChild<Spacer>();
    } else {
        row->emplaceChild<Spacer>();
    }

    // Right label (output)
    if (!right_name.empty()) {
        row->emplaceChild<Label>(right_name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR,
                                 TextAlign::Right);
    }

    // Port circles are added outside the Row so they don't affect label layout.
    // Post-layout snap in layout() positions them at node edges.
    if (!left_name.empty()) {
        auto* port_w = row_container->emplaceChild<Port>(left_name, PortSide::Input, left_type);
        ports_.push_back(port_w);
    }
    if (!right_name.empty()) {
        auto* port_w = row_container->emplaceChild<Port>(right_name, PortSide::Output, right_type);
        ports_.push_back(port_w);
    }
}

void NodeWidget::buildPortInColumn(Widget* col, std::string_view name,
                                   PortType type, bool is_left) {
    using namespace editor_constants;

    constexpr float v_pad = (PORT_ROW_HEIGHT - PORT_LABEL_FONT_SIZE) / 2.0f;
    constexpr float label_indent = PORT_RADIUS * 2 + PORT_LABEL_GAP;
    auto* container = col->emplaceChild<Container>(
        Edges{label_indent, v_pad, label_indent, v_pad});
    auto* row = container->emplaceChild<Row>();

    if (is_left) {
        row->emplaceChild<Label>(name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR);
    } else {
        row->emplaceChild<Spacer>();
        row->emplaceChild<Label>(name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR,
                                 TextAlign::Right);
    }

    auto* port_w = container->emplaceChild<Port>(name,
        is_left ? PortSide::Input : PortSide::Output, type);
    ports_.push_back(port_w);
}

void NodeWidget::buildFourSidedLayout(const ::Node& data, const ui::StringInterner& interner) {
    using namespace editor_constants;
    
    ResolvedLayout layout = resolve_port_layout(data.inputs, data.outputs, 
                                                 data.layout_overrides, interner);
    
    // Top port strip
    if (!layout.top.empty()) {
        buildHorizontalPortStrip(layout.top);
    }
    
    // Main body row: [Left ports | Content | Right ports]
    auto* body_row = layout_->emplaceChild<Row>();
    body_row->setFlexible(true);
    
    // Left column (input ports that stay on left)
    auto* left_col = body_row->emplaceChild<Column>();
    for (const auto& rp : layout.left) {
        buildPortInColumn(left_col, rp.port_name, rp.type, true);
    }
    
    // Content area (flexible spacer if no content)
    auto* center = body_row->emplaceChild<Container>(Edges{10, 0, 10, 0});
    center->setFlexible(true);
    center->emplaceChild<Spacer>();
    
    // Right column (output ports that stay on right)
    auto* right_col = body_row->emplaceChild<Column>();
    for (const auto& rp : layout.right) {
        buildPortInColumn(right_col, rp.port_name, rp.type, false);
    }
    
    // Bottom port strip
    if (!layout.bottom.empty()) {
        buildHorizontalPortStrip(layout.bottom);
    }
}

void NodeWidget::buildHorizontalPortStrip(const std::vector<ResolvedPort>& ports) {
    using namespace editor_constants;
    
    // Container with vertical padding to achieve PORT_ROW_HEIGHT.
    // Ports and labels are positioned horizontally in the layout() post-pass.
    constexpr float v_pad = (PORT_ROW_HEIGHT - PORT_RADIUS * 2) / 2.0f;
    auto* strip = layout_->emplaceChild<Container>(Edges{0, v_pad, 0, v_pad});
    
    bool is_top = (ports.empty()) ? false : 
        (ports[0].layout_side == PortLayoutSide::Top);
    
    for (const auto& rp : ports) {
        // Create port widget - will be positioned in layout() post-pass
        auto* port_w = strip->emplaceChild<Port>(
            rp.port_name, rp.logical_side, rp.type);
        ports_.push_back(port_w);
        
        // Create label widget alongside port - also positioned in post-pass
        auto* label_w = strip->emplaceChild<Label>(
            rp.port_name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR);
        
        if (is_top) {
            top_ports_.push_back(port_w);
            top_port_labels_.push_back(label_w);
        } else {
            bottom_ports_.push_back(port_w);
            bottom_port_labels_.push_back(label_w);
        }
    }
}

void NodeWidget::positionHorizontalPorts(PortLayoutSide side, float node_width) {
    const auto& port_list = (side == PortLayoutSide::Top) ? top_ports_ : bottom_ports_;
    const auto& label_list = (side == PortLayoutSide::Top) ? top_port_labels_ : bottom_port_labels_;
    if (port_list.empty()) return;

    size_t n = port_list.size();
    constexpr float grid = editor_constants::PORT_LAYOUT_GRID;
    float center = node_width / 2.0f;

    for (size_t i = 0; i < n; ++i) {
        auto* p = port_list[i];
        if (!p->parent()) continue;

        // Calculate ideal centered position, then snap to nearest grid crossing.
        // Port i goes at: center + (i - (n-1)/2) * grid
        float ideal_x = center + (static_cast<float>(i) - static_cast<float>(n - 1) / 2.0f) * grid;
        float snapped_x = std::round(ideal_x / grid) * grid;
        
        // Convert to local position relative to parent container
        Pt parent_wp = p->parent()->worldPos();
        Pt node_wp = worldPos();
        float parent_offset_x = parent_wp.x - node_wp.x;
        float lp_x = snapped_x - parent_offset_x - Port::RADIUS;

        // Vertical: snap port center to the node edge.
        float parent_offset_y = parent_wp.y - node_wp.y;
        float lp_y;
        if (side == PortLayoutSide::Top) {
            lp_y = -parent_offset_y - Port::RADIUS;
        } else {
            float node_height = size().y;
            lp_y = node_height - parent_offset_y - Port::RADIUS;
        }

        p->setLocalPos(Pt(lp_x, lp_y));

        // Position the corresponding label centered below (top ports) or above (bottom ports)
        if (i < label_list.size()) {
            auto* lbl = label_list[i];
            if (!lbl) continue;

            float label_w = lbl->preferredSize(nullptr).x;
            float label_x = lp_x + Port::RADIUS - label_w / 2.0f;
            float label_y;
            if (side == PortLayoutSide::Top) {
                label_y = lp_y + Port::RADIUS * 2 + editor_constants::PORT_LABEL_GAP;
            } else {
                label_y = lp_y - editor_constants::PORT_LABEL_FONT_SIZE - editor_constants::PORT_LABEL_GAP;
            }
            lbl->setLocalPos(Pt(label_x, label_y));
        }
    }
}

// ============================================================================
// Content updates
// ============================================================================

void NodeWidget::updateContent(const ::NodeContent& content) {
    if (content_widget_) content_widget_->updateFromContent(content);
}

::Bounds NodeWidget::contentBounds() const {
    if (!content_widget_) return {};
    Pt wp = content_widget_->worldPos();
    Pt np = worldPos();
    Pt sz = content_widget_->size();
    return { wp.x - np.x, wp.y - np.y, sz.x, sz.y };
}

Port* NodeWidget::port(std::string_view name) const {
    for (auto* p : ports_) {
        if (p->name() == name) return p;
    }
    return nullptr;
}

Port* NodeWidget::portByName(std::string_view port_name,
                             std::string_view /*wire_id*/) const {
    for (auto* p : ports_) {
        if (p->name() == port_name) return p;
    }
    return nullptr;
}

// ============================================================================
// Layout & sizing
// ============================================================================

Pt NodeWidget::preferredSize(IDrawList* dl) const {
    if (!layout_) return Pt(0, 0);
    Pt ps = layout_->preferredSize(dl);
    ps.x = std::max(ps.x, editor_constants::MIN_NODE_WIDTH);
    
    // Ensure node is wide enough for top/bottom ports at grid crossings.
    // For n ports, need at least (n + 1) * grid width.
    size_t max_horizontal = std::max(top_ports_.size(), bottom_ports_.size());
    if (max_horizontal > 0) {
        constexpr float grid = editor_constants::PORT_LAYOUT_GRID;
        float min_width_for_ports = static_cast<float>(max_horizontal + 1) * grid;
        ps.x = std::max(ps.x, min_width_for_ports);
    }
    
    return ps;
}

void NodeWidget::layout(float w, float h) {
    setSize(Pt(w, h));
    if (layout_) {
        layout_->layout(w, h);
    }
    // Post-layout: snap port circle centers to node edges and vertically center.
    // Ports live as extra children of their row container (outside Row flow),
    // so they need explicit positioning after layout completes.
    Pt np = worldPos();
    for (auto* p : ports_) {
        Pt wp = p->worldPos();
        Pt lp = p->localPos();
        // Horizontal: snap circle center to node edge
        float current_cx = wp.x + Port::RADIUS;
        if (p->side() == PortSide::Input) {
            lp.x += np.x - current_cx;
        } else if (p->side() == PortSide::Output) {
            lp.x += (np.x + w) - current_cx;
        }
        // Vertical: center port in its parent container
        if (p->parent()) {
            float parent_h = p->parent()->size().y;
            lp.y = (parent_h - Port::RADIUS * 2) / 2.0f;
        }
        p->setLocalPos(lp);
    }

    // Post-layout: distribute top/bottom ports evenly along node width.
    // These override the standard left/right snapping done above.
    positionHorizontalPorts(PortLayoutSide::Top, w);
    positionHorizontalPorts(PortLayoutSide::Bottom, w);
}

// ============================================================================
// Rendering
// ============================================================================

void NodeWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = worldPos();
    Pt sz = size();
    float zoom = ctx.zoom;

    Pt screen_min = ctx.world_to_screen(pos);
    Pt screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float rounding = editor_constants::NODE_ROUNDING * zoom;

    // Body fill
    uint32_t fill = custom_fill_.value_or(render_theme::COLOR_BODY_FILL);
    dl->add_rect_filled_with_rounding(screen_min, screen_max, fill, rounding);

    // Children (header, ports, content, footer) rendered by renderTree()
}

void NodeWidget::renderPost(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = worldPos();
    Pt sz = size();
    Pt screen_min = ctx.world_to_screen(pos);
    Pt screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float rounding = editor_constants::NODE_ROUNDING * ctx.zoom;

    // Selection border drawn after children so it appears on top
    handle_renderer::draw_selection_border(*dl, ctx, *this, screen_min, screen_max, rounding);
}

} // namespace visual
