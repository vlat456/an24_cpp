#include "visual/node/widget/content/switch_widget.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"

SwitchWidget::SwitchWidget(bool state, bool tripped)
    : state_(state), tripped_(tripped)
{
    flexible_ = false;
    height_ = HEIGHT;
}

Pt SwitchWidget::getPreferredSize(IDrawList*) const {
    return Pt(MIN_WIDTH, HEIGHT);
}

void SwitchWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    float w = width_ * zoom;
    float h = HEIGHT * zoom;
    float r = ROUNDING * zoom;

    Pt min = origin;
    Pt max(origin.x + w, origin.y + h);

    uint32_t fill;
    if (tripped_) {
        fill = render_theme::COLOR_TRIPPED;
    } else if (state_) {
        fill = 0xFF3A6830;
    } else {
        fill = 0xFF1C1D24;
    }

    dl->add_rect_filled_with_rounding(min, max, fill, r);
    dl->add_rect_with_rounding_corners(min, max, render_theme::COLOR_BUS_BORDER, r, 0xF0, 1.0f);

    const char* label = tripped_ ? "TRIP" : (state_ ? "ON" : "OFF");
    float font = FONT_SIZE * zoom;
    Pt text_size = dl->calc_text_size(label, font);
    float tx = origin.x + (w - text_size.x) / 2.0f;
    float ty = origin.y + (h - font) / 2.0f;
    uint32_t text_color = tripped_ ? 0xFFFFFFFF : render_theme::COLOR_TEXT;
    dl->add_text(Pt(tx, ty), label, text_color, font);
}
