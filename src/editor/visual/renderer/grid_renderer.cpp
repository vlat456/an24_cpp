#include "visual/renderer/grid_renderer.h"
#include "visual/renderer/render_theme.h"
#include <cmath>

namespace visual {

namespace {

/// Draw a dotted line from screen_a to screen_b using short segments.
/// Dot pattern: `dash` pixels drawn, `gap` pixels skipped.
void draw_dotted_line(IDrawList& dl, Pt screen_a, Pt screen_b,
                      uint32_t color, float thickness, float dash, float gap) {
    float dx = screen_b.x - screen_a.x;
    float dy = screen_b.y - screen_a.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f) return;

    float nx = dx / len;
    float ny = dy / len;
    float period = dash + gap;
    float pos = 0.0f;

    while (pos + dash <= len) {
        Pt seg_start(screen_a.x + nx * pos, screen_a.y + ny * pos);
        float seg_end_pos = pos + dash;
        Pt seg_end(screen_a.x + nx * seg_end_pos, screen_a.y + ny * seg_end_pos);
        dl.add_line(seg_start, seg_end, color, thickness);
        pos += period;
    }

    // Final partial dash if enough remains
    if (pos < len) {
        float partial = std::min(dash, len - pos);
        Pt seg_start(screen_a.x + nx * pos, screen_a.y + ny * pos);
        Pt seg_end(screen_a.x + nx * (pos + partial), screen_a.y + ny * (pos + partial));
        dl.add_line(seg_start, seg_end, color, thickness);
    }
}

} // namespace

void GridRenderer::render(IDrawList& dl, const Viewport& vp, Pt canvas_min, Pt canvas_max) const {
    float step = vp.grid_step;

    Pt tl = vp.screen_to_world(canvas_min, canvas_min);
    Pt br = vp.screen_to_world(canvas_max, canvas_min);

    int x0 = static_cast<int>(std::floor(tl.x / step));
    int x1 = static_cast<int>(std::ceil(br.x / step));
    int y0 = static_cast<int>(std::floor(tl.y / step));
    int y1 = static_cast<int>(std::ceil(br.y / step));

    // == Whole-grid (solid) ==
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

    // == Half-grid (dotted, tinted, offset by step/2) ==
    // Only draw when zoomed in enough for half-grid to be visible.
    float half_step = step * 0.5f;
    float half_step_screen = half_step * vp.zoom;
    if (half_step_screen < 4.0f) return;  // too dense to see

    constexpr float half_line_width = 0.25f;
    constexpr float dot_dash = 3.0f;   // pixels drawn per dot segment
    constexpr float dot_gap  = 3.0f;   // pixels gap between dot segments

    // Vertical half-grid lines
    float hx_start = std::floor(tl.x / step) * step + half_step;
    for (float wx = hx_start; wx <= br.x; wx += step) {
        Pt screen_a = vp.world_to_screen(Pt(wx, tl.y), canvas_min);
        Pt screen_b = vp.world_to_screen(Pt(wx, br.y), canvas_min);
        draw_dotted_line(dl, screen_a, screen_b,
                         render_theme::COLOR_GRID_HALF, half_line_width, dot_dash, dot_gap);
    }

    // Horizontal half-grid lines
    float hy_start = std::floor(tl.y / step) * step + half_step;
    for (float wy = hy_start; wy <= br.y; wy += step) {
        Pt screen_a = vp.world_to_screen(Pt(tl.x, wy), canvas_min);
        Pt screen_b = vp.world_to_screen(Pt(br.x, wy), canvas_min);
        draw_dotted_line(dl, screen_a, screen_b,
                         render_theme::COLOR_GRID_HALF, half_line_width, dot_dash, dot_gap);
    }
}

} // namespace visual
