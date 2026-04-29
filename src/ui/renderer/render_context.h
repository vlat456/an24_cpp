#pragma once

#include "ui/math/pt.h"
#include <cstdint>

namespace ui {

struct RenderContext {
    float dt = 0.0f;
    float zoom = 1.0f;
    Pt pan{0, 0};
    Pt canvas_min{0, 0};
    Pt mouse_pos{0, 0};
    
    uint64_t hovered_id = 0;
    uint64_t selected_id = 0;
    
    bool is_dragging = false;
    
    Pt world_to_screen(Pt world) const {
        return Pt((world.x - pan.x) * zoom + canvas_min.x,
                  (world.y - pan.y) * zoom + canvas_min.y);
    }
};

} // namespace ui
