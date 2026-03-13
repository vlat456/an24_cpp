#include "visual/renderer/node_frame.h"
#include "visual/port/visual_port.h"
#include "visual/renderer/render_theme.h"
#include "editor/layout_constants.h"
#include "data/node.h"

namespace node_frame {

ScreenBounds world_to_screen(Pt world_pos, Pt world_size, 
                             const Viewport& vp, Pt canvas_min) {
    Pt screen_min = vp.world_to_screen(world_pos, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(world_pos.x + world_size.x, world_pos.y + world_size.y), canvas_min);
    return {screen_min, screen_max};
}

void render_ports(IDrawList& dl, const Viewport& vp, Pt canvas_min,
                  const std::vector<visual::Port>& ports) {
    float port_radius = editor_constants::PORT_RADIUS * vp.zoom;
    for (const auto& port : ports) {
        Pt screen_pos = vp.world_to_screen(port.worldPos(), canvas_min);
        uint32_t port_color = render_theme::get_port_color(port.type());
        dl.add_circle_filled(screen_pos, port_radius, port_color, 8);
    }
}

void render_border(IDrawList& dl, const ScreenBounds& bounds,
                   float rounding, bool is_selected) {
    uint32_t border_color = is_selected 
        ? render_theme::COLOR_SELECTED 
        : render_theme::COLOR_BUS_BORDER;
    dl.add_rect_with_rounding_corners(bounds.min, bounds.max, border_color,
                                       rounding, editor_constants::DRAW_CORNERS_ALL, 1.0f);
}

void render_fill(IDrawList& dl, const ScreenBounds& bounds,
                 float rounding, uint32_t fill_color) {
    dl.add_rect_filled_with_rounding(bounds.min, bounds.max, fill_color, rounding);
}

uint32_t get_fill_color(const std::optional<NodeColor>& custom_color,
                        uint32_t default_color) {
    return custom_color.has_value()
        ? custom_color->to_uint32()
        : default_color;
}

}
