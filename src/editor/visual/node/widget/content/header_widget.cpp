#include "visual/node/widget/content/header_widget.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"

HeaderWidget::HeaderWidget(const std::string& name, uint32_t fill_color, float rounding)
    : name_(name), fill_color_(fill_color), rounding_(rounding)
{
    height_ = HEIGHT;
}

Pt HeaderWidget::getPreferredSize(IDrawList* dl) const {
    float name_w = 0;
    if (!name_.empty()) {
        if (dl) {
            Pt text_size = dl->calc_text_size(name_.c_str(), FONT_SIZE);
            name_w = text_size.x;
        } else {
            name_w = estimateTextWidth(name_);
        }
    }
    float total_w = PADDING + name_w + PADDING;
    return Pt(total_w, HEIGHT);
}

void HeaderWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    float w = width_ * zoom;
    float vh = VISUAL_HEIGHT * zoom;
    float r = rounding_ * zoom;

    if (r > 0.0f) {
        dl->add_rect_filled_with_rounding_corners(
            origin, Pt(origin.x + w, origin.y + vh), fill_color_, r, 0x30);
    } else {
        dl->add_rect_filled(origin, Pt(origin.x + w, origin.y + vh), fill_color_);
    }

    float font = FONT_SIZE * zoom;
    Pt text_pos(origin.x + PADDING * zoom, origin.y + vh / 2 - font / 2);
    dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_TEXT, font);
}
