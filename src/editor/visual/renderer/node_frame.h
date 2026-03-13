#pragma once

#include "data/pt.h"
#include "viewport/viewport.h"
#include "visual/renderer/draw_list.h"
#include <optional>
#include <vector>

namespace visual { class Port; }
struct NodeColor;

namespace node_frame {

struct ScreenBounds {
    Pt min, max;
    Pt center() const { return Pt((min.x + max.x) / 2, (min.y + max.y) / 2); }
    float width() const { return max.x - min.x; }
    float height() const { return max.y - min.y; }
};

ScreenBounds world_to_screen(Pt world_pos, Pt world_size, 
                             const Viewport& vp, Pt canvas_min);

void render_ports(IDrawList& dl, const Viewport& vp, Pt canvas_min,
                  const std::vector<visual::Port>& ports);

void render_border(IDrawList& dl, const ScreenBounds& bounds,
                   float rounding, bool is_selected);

void render_fill(IDrawList& dl, const ScreenBounds& bounds,
                 float rounding, uint32_t fill_color);

uint32_t get_fill_color(const std::optional<NodeColor>& custom_color,
                        uint32_t default_color);

}
