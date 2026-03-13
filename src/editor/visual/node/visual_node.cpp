#include "visual_node.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/handle_renderer.h"
#include "visual/render_context.h"
#include "editor/layout_constants.h"
#include "visual/node/bounds.h"
#include "data/node.h"
#include <algorithm>
#include <cmath>

namespace visual {

// ============================================================================
// Construction
// ============================================================================

NodeWidget::NodeWidget(const ::Node& data)
    : node_id_(data.id)
    , name_(data.name)
    , type_name_(data.type_name)
{
    if (data.color.has_value()) {
        custom_fill_ = data.color->to_uint32();
    }

    setLocalPos(data.pos);
    buildLayout(data);

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
    auto snap = [](float v) {
        constexpr float g = editor_constants::PORT_LAYOUT_GRID;
        return std::ceil(v / g) * g;
    };
    w = snap(w);
    h = snap(h);

    setSize(Pt(w, h));
    layout_->layout(w, h);
}

// ============================================================================
// Layout construction
// ============================================================================

void NodeWidget::buildLayout(const ::Node& data) {
    layout_ = emplaceChild<Column>();

    // -- Header --
    layout_->emplaceChild<HeaderWidget>(
        name_, render_theme::COLOR_HEADER_FILL, editor_constants::NODE_ROUNDING);

    // -- Port rows / Content --
    if (data.node_content.type == NodeContentType::VerticalToggle) {
        buildVerticalToggleLayout(data);
    } else {
        buildStandardLayout(data);
    }

    // -- Type name footer --
    layout_->emplaceChild<TypeNameWidget>(type_name_);
}

void NodeWidget::buildStandardLayout(const ::Node& data) {
    // Port rows: pair inputs and outputs
    size_t max_ports = std::max(data.inputs.size(), data.outputs.size());
    for (size_t i = 0; i < max_ports; i++) {
        const std::string* left_name = (i < data.inputs.size()) ? &data.inputs[i].name : nullptr;
        PortType left_type = (i < data.inputs.size()) ? data.inputs[i].type : PortType::Any;
        const std::string* right_name = (i < data.outputs.size()) ? &data.outputs[i].name : nullptr;
        PortType right_type = (i < data.outputs.size()) ? data.outputs[i].type : PortType::Any;
        buildPortRow(left_name, left_type, right_name, right_type);
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

void NodeWidget::buildVerticalToggleLayout(const ::Node& data) {
    auto* main_row = layout_->emplaceChild<Row>();
    main_row->setFlexible(true);

    // Left column (input ports)
    auto* left_col = main_row->emplaceChild<Column>();
    for (const auto& p : data.inputs) {
        buildPortInColumn(left_col, p.name, p.type, true);
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
        buildPortInColumn(right_col, p.name, p.type, false);
    }
}

void NodeWidget::buildPortRow(const std::string* left_name, PortType left_type,
                              const std::string* right_name, PortType right_type) {
    using namespace editor_constants;

    // Build port row: [Port? Label? Spacer Label? Port?]
    // Wrapped in Container for vertical padding to center within PORT_ROW_HEIGHT
    auto* row_container = layout_->emplaceChild<Container>(Edges{0, 0, 0, 0});
    auto* row = row_container->emplaceChild<Row>();

    // Left port (input)
    if (left_name) {
        auto* port_w = row->emplaceChild<Port>(*left_name, PortSide::Input, left_type);
        port_w->setLocalPos(Pt(-PORT_RADIUS, 0));
        ports_.push_back(port_w);

        row->emplaceChild<Label>(*left_name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR);
    }

    // Flexible spacer
    if (left_name && right_name) {
        auto* gap = row->emplaceChild<Container>(
            Edges{PORT_MIN_GAP / 2.0f, 0, PORT_MIN_GAP / 2.0f, 0});
        gap->setFlexible(true);
        gap->emplaceChild<Spacer>();
    } else {
        row->emplaceChild<Spacer>();
    }

    // Right port (output)
    if (right_name) {
        row->emplaceChild<Label>(*right_name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR,
                                 TextAlign::Right);

        auto* port_w = row->emplaceChild<Port>(*right_name, PortSide::Output, right_type);
        port_w->setLocalPos(Pt(PORT_RADIUS, 0));
        ports_.push_back(port_w);
    }
}

void NodeWidget::buildPortInColumn(Widget* col, const std::string& name,
                                   PortType type, bool is_left) {
    using namespace editor_constants;

    auto* container = col->emplaceChild<Container>(Edges{0, 0, 0, 0});
    auto* row = container->emplaceChild<Row>();

    if (is_left) {
        auto* port_w = row->emplaceChild<Port>(name, PortSide::Input, type);
        port_w->setLocalPos(Pt(-PORT_RADIUS, 0));
        ports_.push_back(port_w);
        row->emplaceChild<Label>(name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR);
    } else {
        row->emplaceChild<Label>(name, PORT_LABEL_FONT_SIZE, PORT_LABEL_COLOR,
                                 TextAlign::Right);
        auto* port_w = row->emplaceChild<Port>(name, PortSide::Output, type);
        port_w->setLocalPos(Pt(PORT_RADIUS, 0));
        ports_.push_back(port_w);
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

Port* NodeWidget::port(const std::string& name) const {
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
    return ps;
}

void NodeWidget::layout(float w, float h) {
    setSize(Pt(w, h));
    if (layout_) {
        layout_->layout(w, h);
    }
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
