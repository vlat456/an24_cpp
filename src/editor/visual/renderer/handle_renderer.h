#pragma once

#include "visual/renderer/draw_list.h"
#include "data/pt.h"
#include <cstdint>

/// Unified visual handle drawing — DRY for routing points, resize handles, etc.
/// Each handle is a filled circle with a dark outline.
namespace handle_renderer {

inline void draw_handle(IDrawList& dl, Pt screen_pos, float radius,
                        uint32_t fill_color, uint32_t outline_color = 0xFF000000,
                        int segments = 12) {
    dl.add_circle_filled(screen_pos, radius, fill_color, segments);
    dl.add_circle(screen_pos, radius, outline_color, segments);
}

}  // namespace handle_renderer
