#pragma once

#include "visual/renderer/draw_list.h"
#include "visual/widget.h"
#include "visual/render_context.h"
#include "visual/renderer/render_theme.h"
#include "editor/layout_constants.h"
#include "ui/math/pt.h"
#include <cstdint>

/// Unified visual handle drawing — DRY for routing points, resize handles, etc.
/// Each handle is a filled circle with a dark outline.
namespace handle_renderer {

using ui::IDrawList;
using ui::Pt;

inline void draw_handle(IDrawList& dl, Pt screen_pos, float radius,
                        uint32_t fill_color, uint32_t outline_color = 0xFF000000,
                        int segments = 12) {
    dl.add_circle_filled(screen_pos, radius, fill_color, segments);
    dl.add_circle(screen_pos, radius, outline_color, segments);
}

/// Draw selection border around a widget's screen-space bounds.
/// Replaces the duplicated 4-line selection-border pattern in every widget render().
inline void draw_selection_border(IDrawList& dl, const visual::RenderContext& ctx,
                                  const visual::Widget& w,
                                  Pt screen_min, Pt screen_max, float rounding) {
    if (!ctx.isNodeSelected(&w)) return;
    dl.add_rect_with_rounding_corners(screen_min, screen_max,
        render_theme::COLOR_SELECTED, rounding,
        editor_constants::DRAW_CORNERS_ALL, 2.0f * ctx.zoom);
}

/// Draw resize handles at the four corners of a resizable widget when selected.
inline void draw_resize_handles(IDrawList& dl, const visual::RenderContext& ctx,
                                const visual::Widget& w) {
    if (!ctx.isNodeSelected(&w)) return;

    Pt mn = w.worldMin();
    Pt mx = w.worldMax();
    float r = editor_constants::RESIZE_HANDLE_SIZE * 0.5f * ctx.zoom;
    uint32_t color = render_theme::COLOR_RESIZE_HANDLE;

    Pt corners[] = {
        ctx.world_to_screen(mn),
        ctx.world_to_screen(Pt(mx.x, mn.y)),
        ctx.world_to_screen(Pt(mn.x, mx.y)),
        ctx.world_to_screen(mx),
    };

    for (const auto& c : corners) {
        draw_handle(dl, c, r, color);
    }
}

}  // namespace handle_renderer
