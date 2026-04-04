#include "ref_node_widget.h"
#include "visual/render_context.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/handle_renderer.h"
#include "editor/layout_constants.h"
#include "data/node_content.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace visual {

// ============================================================================
// Construction
// ============================================================================

/// Format a float for display: no trailing zeros, up to 6 significant digits.
static std::string format_value(float v) {
    // Use %g for compact representation (no trailing zeros)
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
    return std::string(buf);
}

RefNodeWidget::RefNodeWidget(const bp2::Blueprint::Node& data, const ui::StringInterner& interner)
    : node_iid_(data.id)
    , interner_(&interner)
    , name_(data.name)
    , type_name_(std::string(interner.resolve(data.type)))
{
    // For Value nodes, display the numeric value instead of the name
    if (type_name_ == "Value" && !data.params.empty()) {
        name_ = format_value(data.params.begin()->second);
    }
    if (data.has_color) {
        NodeColor c;
        c.r = data.color_r;
        c.g = data.color_g;
        c.b = data.color_b;
        c.a = data.color_a;
        custom_fill_ = c.to_uint32();
    }

    setLocalPos(Pt(data.x, data.y));
    buildLayout(data, interner);

    // Size based on text width + horizontal padding
    float text_w = name_.empty() ? 0.0f : name_.length() * PortConstants::LABEL_FONT_SIZE * 0.8f;
    constexpr float h_pad = 16.0f;
    constexpr float v_pad = 4.0f;
    Pt node_size(text_w + h_pad, PortConstants::LABEL_FONT_SIZE + v_pad);
    setSize(node_size);
    positionPort();
}

// ============================================================================
// Layout
// ============================================================================

void RefNodeWidget::buildLayout(const bp2::Blueprint::Node& data, const ui::StringInterner& interner) {
    // Determine the single port from node data.
    // Port stores a string_view, so the name must point to stable storage
    // (the interner) — never to a local std::string.
    std::string_view port_name = "v";
    PortType port_type = PortType::V;

    if (!data.outputs.empty()) {
        port_name = interner.resolve(data.outputs[0].name);
        port_type = data.outputs[0].type;
    } else if (!data.inputs.empty()) {
        port_name = interner.resolve(data.inputs[0].name);
        port_type = data.inputs[0].type;
    }

    // Single port, centered on top edge
    port_ = emplaceChild<Port>(port_name, PortSide::Output, port_type);
}

void RefNodeWidget::positionPort() {
    if (!port_) return;

    const float center_x = size().x * 0.5f;
    const float center_y = size().y * 0.5f;
    const float clamped_center_x = std::clamp(center_x,
                                              PortConstants::RADIUS,
                                              size().x - PortConstants::RADIUS);
    const float clamped_center_y = std::clamp(center_y,
                                              PortConstants::RADIUS,
                                              size().y - PortConstants::RADIUS);

    Pt local_pos;
    switch (port_layout_side_) {
        case PortLayoutSide::Left:
            local_pos = Pt(-PortConstants::RADIUS,
                           clamped_center_y - PortConstants::RADIUS);
            break;
        case PortLayoutSide::Right:
            local_pos = Pt(size().x - PortConstants::RADIUS,
                           clamped_center_y - PortConstants::RADIUS);
            break;
        case PortLayoutSide::Bottom:
            local_pos = Pt(clamped_center_x - PortConstants::RADIUS,
                           size().y - PortConstants::RADIUS);
            break;
        case PortLayoutSide::Top:
        default:
            local_pos = Pt(clamped_center_x - PortConstants::RADIUS,
                           -PortConstants::RADIUS);
            break;
    }

    port_->setLayoutSide(port_layout_side_);
    port_->setLocalPos(local_pos);
}

void RefNodeWidget::setPortLayoutSide(PortLayoutSide side) {
    if (port_layout_side_ == side) return;
    port_layout_side_ = side;
    positionPort();
}

Port* RefNodeWidget::port(std::string_view name) const {
    if (port_ && port_->name() == name) return port_;
    return nullptr;
}

Port* RefNodeWidget::portByName(std::string_view port_name,
                                std::string_view /*wire_id*/) const {
    if (port_ && port_->name() == port_name) return port_;
    return nullptr;
}

Pt RefNodeWidget::preferredSize(IDrawList* dl) const {
    if (!dl) return Pt(56.0f, 20.0f);
    float w = dl->calc_text_size(name_.c_str(), PortConstants::LABEL_FONT_SIZE).x;
    constexpr float h_pad = 16.0f;
    constexpr float v_pad = 4.0f;
    return Pt(w + h_pad, PortConstants::LABEL_FONT_SIZE + v_pad);
}

void RefNodeWidget::layout(float w, float h) {
    setSize(Pt(w, h));
    positionPort();
}

// ============================================================================
// Rendering
// ============================================================================

void RefNodeWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = worldPos();
    Pt sz = size();
    float zoom = ctx.zoom;

    Pt screen_min = ctx.world_to_screen(pos);
    Pt screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float rounding = editor_constants::NODE_ROUNDING * zoom;

    // Body fill
    uint32_t fill = custom_fill_.value_or(render_theme::COLOR_BUS_FILL);
    dl->add_rect_filled_with_rounding(screen_min, screen_max, fill, rounding);

    // Border
    uint32_t border_color = render_theme::COLOR_BUS_BORDER;
    dl->add_rect_with_rounding_corners(screen_min, screen_max, border_color, rounding,
                                       editor_constants::DRAW_CORNERS_ALL, 1.0f);

    // Value text, vertically centered
    float font_size = PortConstants::LABEL_FONT_SIZE * zoom;
    float text_h = dl->calc_text_size(name_.c_str(), font_size).y;
    float text_y = screen_min.y + (sz.y * zoom - text_h) / 2.0f;
    float text_x = screen_min.x + 2.0f * zoom;
    dl->add_text(Pt(text_x, text_y), name_.c_str(), render_theme::COLOR_TEXT, font_size);
}

void RefNodeWidget::renderPost(IDrawList* dl, const RenderContext& ctx) const {
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
