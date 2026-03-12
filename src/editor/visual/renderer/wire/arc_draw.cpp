#include "arc_draw.h"
#include "visual/renderer/render_theme.h"
#include <cmath>

namespace arc_draw {

static constexpr float PI = 3.14159265f;

void draw_jump_arc(IDrawList& dl, const WireCrossing& crossing,
                   Pt screen_pos, float arc_radius, uint32_t wire_color) {
    using namespace render_theme;
    
    bool arc_vertical = (crossing.my_seg_dir == SegDir::Horiz ||
                         crossing.my_seg_dir == SegDir::Unknown);

    Pt arc_points[ARC_SEGMENTS + 1];
    for (int i = 0; i <= ARC_SEGMENTS; i++) {
        float angle = PI * i / ARC_SEGMENTS;
        if (arc_vertical) {
            Pt offset(std::cos(angle) * arc_radius, std::sin(angle) * arc_radius);
            arc_points[i] = Pt(screen_pos.x + offset.x, screen_pos.y - offset.y);
        } else {
            Pt offset(std::sin(angle) * arc_radius, std::cos(angle) * arc_radius);
            arc_points[i] = Pt(screen_pos.x + offset.x, screen_pos.y + offset.y);
        }
    }
    dl.add_polyline(arc_points, ARC_SEGMENTS + 1, wire_color, 2.0f);
}

void draw_all_arcs(IDrawList& dl, const std::vector<WireCrossing>& crossings,
                   const Viewport& vp, Pt canvas_min, uint32_t wire_color) {
    using namespace render_theme;
    
    for (const auto& crossing : crossings) {
        Pt screen_cross = vp.world_to_screen(crossing.pos, canvas_min);
        float arc_radius = ARC_RADIUS_WORLD * vp.zoom;
        draw_jump_arc(dl, crossing, screen_cross, arc_radius, wire_color);
    }
}

}
