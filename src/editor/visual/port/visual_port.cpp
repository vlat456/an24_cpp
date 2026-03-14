#include "visual_port.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"
#include "visual/render_context.h"

namespace visual {

Port::Port(std::string_view name, PortSide side, PortType type)
    : name_(name), side_(side), type_(type)
{
    setSize(Pt(RADIUS * 2, RADIUS * 2));
}

uint32_t Port::color() const {
    return render_theme::get_port_color(type_);
}

Pt Port::preferredSize(IDrawList*) const {
    return Pt(RADIUS * 2, RADIUS * 2);
}

void Port::render(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = ctx.world_to_screen(worldPos());
    float r = RADIUS * ctx.zoom;

    // Port circle centered on worldPos
    Pt center(pos.x + r, pos.y + r);
    dl->add_circle_filled(center, r, color(), 8);
}

} // namespace visual
