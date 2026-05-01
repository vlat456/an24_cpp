#include "visual_port.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"
#include "visual/render_context.h"
#include "visual/port/port_arrow.h"
#include <algorithm>

namespace visual {

Port::Port(std::string_view name, bp2::Direction direction, PortType type, bp2::PortLayoutSide layout_side)
    : name_(name), direction_(direction), type_(type), layout_side_(layout_side)
{
    kind_ = ui::WidgetKind::Port;
    setSize(Pt(PortConstants::RADIUS * 2, PortConstants::RADIUS * 2));
}

uint32_t Port::color() const {
    return render_theme::get_port_color(type_);
}

Pt Port::preferredSize(IDrawList*) const {
    return Pt(PortConstants::RADIUS * 2, PortConstants::RADIUS * 2);
}

void Port::render(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = ctx.world_to_screen(worldPos());
    float r = PortConstants::RADIUS * ctx.zoom;
    Pt center(pos.x + r, pos.y + r);
    dl->add_circle_filled(center, r, color(), 8);

    if (direction_ == bp2::Direction::InOut) return;
    float thickness = PortConstants::ARROW_THICKNESS * ctx.zoom;
    const float arrow_offset = r * arrow_offset_for_side(layout_side_);
    const float arrow_size = PortConstants::ARROW_SIZE * ctx.zoom;
    auto arrow = compute_port_arrow(layout_side_, direction_, center, r,
                                    arrow_offset, arrow_size);
    dl->add_line(arrow.tip, arrow.back1, color(), thickness);
    dl->add_line(arrow.tip, arrow.back2, color(), thickness);
}

void Port::renderDebugPaintBounds(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = ctx.world_to_screen(worldPos());
    float r = PortConstants::RADIUS * ctx.zoom;
    Pt center(pos.x + r, pos.y + r);
    dl->add_rect(Pt(center.x - r, center.y - r),
                 Pt(center.x + r, center.y + r),
                 DEBUG_PAINT_BOUNDS_COLOR,
                 1.0f);

    if (direction_ == bp2::Direction::InOut) return;
    const float arrow_offset = r * arrow_offset_for_side(layout_side_);
    const float arrow_size = PortConstants::ARROW_SIZE * ctx.zoom;
    auto arrow = compute_port_arrow(layout_side_, direction_, center, r,
                                    arrow_offset, arrow_size);
    float min_x = std::min({arrow.tip.x, arrow.back1.x, arrow.back2.x});
    float min_y = std::min({arrow.tip.y, arrow.back1.y, arrow.back2.y});
    float max_x = std::max({arrow.tip.x, arrow.back1.x, arrow.back2.x});
    float max_y = std::max({arrow.tip.y, arrow.back1.y, arrow.back2.y});
    dl->add_rect(Pt(min_x, min_y), Pt(max_x, max_y), DEBUG_PAINT_BOUNDS_COLOR, 1.0f);
}

} // namespace visual
