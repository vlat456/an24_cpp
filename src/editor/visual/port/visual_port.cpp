#include "visual_port.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"
#include "visual/render_context.h"

namespace visual {

namespace {

struct PortArrowGeometry {
    Pt tip;
    Pt back1;
    Pt back2;
};

PortArrowGeometry compute_port_arrow_geometry(const Port& port, Pt center, float radius, float zoom) {
    float arrow_offset_mult = 0;
    switch (port.layoutSide()) {
        case bp2::PortLayoutSide::Left:   arrow_offset_mult = PortConstants::LEFT_ARROW_OFFSET; break;
        case bp2::PortLayoutSide::Right:  arrow_offset_mult = PortConstants::RIGHT_ARROW_OFFSET; break;
        case bp2::PortLayoutSide::Top:    arrow_offset_mult = PortConstants::TOP_ARROW_OFFSET; break;
        case bp2::PortLayoutSide::Bottom: arrow_offset_mult = PortConstants::BOTTOM_ARROW_OFFSET; break;
    }

    const float arrow_offset = radius * arrow_offset_mult;
    const float arrow_size = PortConstants::ARROW_SIZE * zoom;
    const bool is_output = (port.side() == bp2::Direction::Output);
    const bool horizontal = (port.layoutSide() == bp2::PortLayoutSide::Left ||
                             port.layoutSide() == bp2::PortLayoutSide::Right);

    PortArrowGeometry geometry{};
    if (horizontal) {
        const bool interior_is_positive = (port.layoutSide() == bp2::PortLayoutSide::Left);
        const float interior_sign = interior_is_positive ? 1.0f : -1.0f;
        const float arrow_x = center.x + interior_sign * arrow_offset;
        const float tip_sign = is_output ? -interior_sign : interior_sign;
        geometry.tip = Pt(arrow_x, center.y);
        geometry.back1 = Pt(arrow_x - tip_sign * arrow_size, center.y - arrow_size);
        geometry.back2 = Pt(arrow_x - tip_sign * arrow_size, center.y + arrow_size);
    } else {
        const bool interior_is_positive = (port.layoutSide() == bp2::PortLayoutSide::Top);
        const float interior_sign = interior_is_positive ? 1.0f : -1.0f;
        const float arrow_y = center.y + interior_sign * arrow_offset;
        const float tip_sign = is_output ? -interior_sign : interior_sign;
        geometry.tip = Pt(center.x, arrow_y);
        geometry.back1 = Pt(center.x - arrow_size, arrow_y - tip_sign * arrow_size);
        geometry.back2 = Pt(center.x + arrow_size, arrow_y - tip_sign * arrow_size);
    }
    return geometry;
}

} // namespace

Port::Port(std::string_view name, bp2::Direction side, PortType type, bp2::PortLayoutSide layout_side)
    : name_(name), side_(side), type_(type), layout_side_(layout_side)
{
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

    if (side_ == bp2::Direction::InOut) return;
    float thickness = PortConstants::ARROW_THICKNESS * ctx.zoom;
    const PortArrowGeometry arrow = compute_port_arrow_geometry(*this, center, r, ctx.zoom);
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

    if (side_ == bp2::Direction::InOut) return;
    const PortArrowGeometry arrow = compute_port_arrow_geometry(*this, center, r, ctx.zoom);
    float min_x = std::min({arrow.tip.x, arrow.back1.x, arrow.back2.x});
    float min_y = std::min({arrow.tip.y, arrow.back1.y, arrow.back2.y});
    float max_x = std::max({arrow.tip.x, arrow.back1.x, arrow.back2.x});
    float max_y = std::max({arrow.tip.y, arrow.back1.y, arrow.back2.y});
    dl->add_rect(Pt(min_x, min_y), Pt(max_x, max_y), DEBUG_PAINT_BOUNDS_COLOR, 1.0f);
}

} // namespace visual
