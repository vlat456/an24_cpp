#include "visual/node/widget/content/type_name_widget.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"

TypeNameWidget::TypeNameWidget(const std::string& type_name)
    : type_name_(type_name)
{
    height_ = HEIGHT;
}

Pt TypeNameWidget::getPreferredSize(IDrawList* dl) const {
    float name_w = 0;
    if (!type_name_.empty()) {
        if (dl) {
            Pt text_size = dl->calc_text_size(type_name_.c_str(), FONT_SIZE);
            name_w = text_size.x;
        } else {
            name_w = type_name_.length() * FONT_SIZE * 0.6f;
        }
    }
    return Pt(name_w, HEIGHT);
}

void TypeNameWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    float w = width_ * zoom;
    float font = FONT_SIZE * zoom;

    Pt text_size = dl->calc_text_size(type_name_.c_str(), font);
    float tx = origin.x + (w - text_size.x) / 2;
    float ty = origin.y + (height_ * zoom - font) / 2;
    dl->add_text(Pt(tx, ty), type_name_.c_str(), render_theme::COLOR_TEXT_DIM, font);
}
