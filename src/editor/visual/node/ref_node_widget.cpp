#include "ref_node_widget.h"
#include "visual/render_context.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/handle_renderer.h"
#include "editor/layout_constants.h"
#include "data/node.h"
#include <algorithm>
#include <cmath>

namespace visual {

// ============================================================================
// Construction
// ============================================================================

RefNodeWidget::RefNodeWidget(const ::Node& data, const ui::StringInterner& interner)
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

    // Snap size to grid
    auto snap = [](float v) {
        constexpr float g = editor_constants::PORT_LAYOUT_GRID;
        return std::ceil(v / g) * g;
    };

    float w = snap(std::max(data.size.x, 48.0f));
    float h = snap(std::max(data.size.y, 32.0f));
    setSize(Pt(w, h));

    positionPort();
}

// ============================================================================
// Layout
// ============================================================================

void RefNodeWidget::buildLayout(const ::Node& data, const ui::StringInterner& interner) {
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
    // Port centered horizontally, circle center on top edge (y=0)
    port_->setLocalPos(Pt(size().x / 2.0f - editor_constants::PORT_RADIUS,
                          -editor_constants::PORT_RADIUS));
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

Pt RefNodeWidget::preferredSize(IDrawList* /*dl*/) const {
    return Pt(48.0f, 32.0f);
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

    // Name text (centered vertically, left-aligned with small padding)
    Pt center = Pt((screen_min.x + screen_max.x) / 2.0f,
                   (screen_min.y + screen_max.y) / 2.0f);
    Pt text_pos(screen_min.x + 2.0f * zoom, center.y - 5.0f * zoom);
    dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_TEXT, 10.0f * zoom);

    // Border
    uint32_t border_color = render_theme::COLOR_BUS_BORDER;
    dl->add_rect_with_rounding_corners(screen_min, screen_max, border_color, rounding,
                                       editor_constants::DRAW_CORNERS_ALL, 1.0f);

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
