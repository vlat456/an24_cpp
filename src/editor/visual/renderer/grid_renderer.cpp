#include "visual/renderer/grid_renderer.h"
#include "visual/renderer/render_theme.h"
#include <cmath>

namespace visual {

void GridRenderer::render(IDrawList& dl, const Viewport& vp, Pt canvas_min, Pt canvas_max) const {
    float step = vp.grid_step;

    Pt tl = vp.screen_to_world(canvas_min, canvas_min);
    Pt br = vp.screen_to_world(canvas_max, canvas_min);

    int x0 = static_cast<int>(std::floor(tl.x / step));
    int x1 = static_cast<int>(std::ceil(br.x / step));
    int y0 = static_cast<int>(std::floor(tl.y / step));
    int y1 = static_cast<int>(std::ceil(br.y / step));

    // == Whole-grid (bright) ==
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

    // == Half-grid (subtle, offset by step/2) ==
    // Only draw when zoomed in enough for half-grid to be visible.
    float half_step = step * 0.5f;
    float half_step_screen = half_step * vp.zoom;
    if (half_step_screen < 4.0f) return;  // too dense to see

    constexpr uint32_t COLOR_GRID_HALF = 0xFF1A1515;  // much darker than COLOR_GRID
    constexpr float half_line_width = 0.25f;

    // Vertical half-grid lines: offset by half_step from whole-grid
    float hx_start = std::floor(tl.x / step) * step + half_step;
    for (float wx = hx_start; wx <= br.x; wx += step) {
        dl.add_line(vp.world_to_screen(Pt(wx, tl.y), canvas_min),
                    vp.world_to_screen(Pt(wx, br.y), canvas_min),
                    COLOR_GRID_HALF, half_line_width);
    }

    // Horizontal half-grid lines
    float hy_start = std::floor(tl.y / step) * step + half_step;
    for (float wy = hy_start; wy <= br.y; wy += step) {
        dl.add_line(vp.world_to_screen(Pt(tl.x, wy), canvas_min),
                    vp.world_to_screen(Pt(br.x, wy), canvas_min),
                    COLOR_GRID_HALF, half_line_width);
    }
}

} // namespace visual
