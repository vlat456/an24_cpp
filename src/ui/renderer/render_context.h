#pragma once

#include "ui/math/pt.h"
#include <cstdint>

namespace ui {

struct RenderContext {
    float dt = 0.0f;
    float zoom = 1.0f;
    Pt pan{0, 0};
    Pt canvas_min{0, 0};
    Pt canvas_max{0, 0};
    Pt mouse_pos{0, 0};

    uint64_t hovered_id = 0;
    uint64_t selected_id = 0;

    bool is_dragging = false;

    Pt world_to_screen(Pt world) const {
        return Pt((world.x - pan.x) * zoom + canvas_min.x,
                  (world.y - pan.y) * zoom + canvas_min.y);
    }

    Pt screen_to_world(Pt screen) const {
        return Pt((screen.x - canvas_min.x) / zoom + pan.x,
                  (screen.y - canvas_min.y) / zoom + pan.y);
    }

    /// Visible world-space bounds for culling. Expanded by margin.
    void compute_viewport(float margin = 30.0f) {
        viewport_min = screen_to_world(Pt(canvas_min.x - margin, canvas_min.y - margin));
        viewport_max = screen_to_world(Pt(canvas_max.x + margin, canvas_max.y + margin));
    }

    bool is_visible(Pt wmin, Pt wmax) const {
        return wmax.x >= viewport_min.x && wmin.x <= viewport_max.x &&
               wmax.y >= viewport_min.y && wmin.y <= viewport_max.y;
    }

    Pt viewport_min{0, 0};
    Pt viewport_max{0, 0};
};

} // namespace ui
