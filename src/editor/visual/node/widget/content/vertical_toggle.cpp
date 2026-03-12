#include "visual/node/widget/content/vertical_toggle.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"

VerticalToggleWidget::VerticalToggleWidget(bool state, bool tripped)
    : state_(state), tripped_(tripped)
{
    flexible_ = false;
    width_ = WIDTH;
    height_ = HEIGHT;
}

Pt VerticalToggleWidget::getPreferredSize(IDrawList*) const {
    return Pt(WIDTH, HEIGHT);
}

void VerticalToggleWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    float w = WIDTH * zoom;
    float h = HEIGHT * zoom;
    float track_w = TRACK_WIDTH * zoom;
    float handle_sz = HANDLE_SIZE * zoom;
    float r = ROUNDING * zoom;

    float cx = origin.x + w / 2.0f;

    float track_x = cx - track_w / 2.0f;
    Pt track_min(track_x, origin.y);
    Pt track_max(track_x + track_w, origin.y + h);
    dl->add_rect_filled_with_rounding(track_min, track_max, 0xFF1C1D24, r);

    float handle_y = state_
        ? origin.y + h * 0.15f
        : origin.y + h * 0.70f;

    Pt handle_min(cx - handle_sz / 2.0f, handle_y);
    Pt handle_max(cx + handle_sz / 2.0f, handle_y + handle_sz);

    uint32_t handle_fill;
    uint32_t handle_border;
    if (tripped_) {
        handle_fill = render_theme::COLOR_TRIPPED;
        handle_border = 0xFF204080;
    } else if (state_) {
        handle_fill = 0xFF3A6830;
        handle_border = 0xFF284820;
    } else {
        handle_fill = 0xFF2C3038;
        handle_border = 0xFF1C1D24;
    }

    dl->add_rect_filled_with_rounding(handle_min, handle_max, handle_fill, r);
    dl->add_rect_with_rounding_corners(handle_min, handle_max, handle_border, r, 0xF0, 1.0f * zoom);

    uint32_t grip_color = tripped_ ? 0xFFFFFFFF : render_theme::COLOR_TEXT_DIM;
    float grip_y = handle_y + handle_sz / 2.0f;
    float grip_w = 2.0f * zoom;
    float grip_h = 6.0f * zoom;
    dl->add_rect_filled(Pt(cx - grip_w / 2.0f, grip_y - grip_h / 2.0f),
                        Pt(cx + grip_w / 2.0f, grip_y + grip_h / 2.0f),
                        grip_color);
}
