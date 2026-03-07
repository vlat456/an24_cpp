#include "visual/renderer/grid_renderer.h"
#include "visual/renderer/render_theme.h"
#include <cmath>

void GridRenderer::render(IDrawList& dl, const Viewport& vp, Pt canvas_min, Pt canvas_max) const {
    float step = vp.grid_step;

    Pt tl = vp.screen_to_world(canvas_min, canvas_min);
    Pt br = vp.screen_to_world(canvas_max, canvas_min);

    int x0 = static_cast<int>(std::floor(tl.x / step));
    int x1 = static_cast<int>(std::ceil(br.x / step));
    int y0 = static_cast<int>(std::floor(tl.y / step));
    int y1 = static_cast<int>(std::ceil(br.y / step));

    float line_width = 0.5f;

    for (int gx = x0; gx <= x1; gx++) {
        Pt wp_start(static_cast<float>(gx) * step, tl.y);
        Pt wp_end(static_cast<float>(gx) * step, br.y);
        dl.add_line(vp.world_to_screen(wp_start, canvas_min),
                    vp.world_to_screen(wp_end, canvas_min),
                    render_theme::COLOR_GRID, line_width);
    }

    for (int gy = y0; gy <= y1; gy++) {
        Pt wp_start(tl.x, static_cast<float>(gy) * step);
        Pt wp_end(br.x, static_cast<float>(gy) * step);
        dl.add_line(vp.world_to_screen(wp_start, canvas_min),
                    vp.world_to_screen(wp_end, canvas_min),
                    render_theme::COLOR_GRID, line_width);
    }
}
