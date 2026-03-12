#include "visual/node/widget/primitives/label.h"
#include "visual/renderer/draw_list.h"

Label::Label(const std::string& text, float font_size, uint32_t color,
             TextAlign align)
    : text_(text), font_size_(font_size), color_(color), align_(align) {}

float Label::estimateWidth() const {
    if (text_.empty()) return 0;
    return text_.length() * font_size_ * 0.6f;
}

Pt Label::getPreferredSize(IDrawList* dl) const {
    float w = dl ? dl->calc_text_size(text_.c_str(), font_size_).x : estimateWidth();
    return Pt(w, font_size_);
}

void Label::render(IDrawList* dl, Pt origin, float zoom) const {
    if (!dl || text_.empty()) return;
    float font = font_size_ * zoom;
    float ty = origin.y + (height_ * zoom - font) / 2;
    float tx = origin.x;
    if (align_ == TextAlign::Right) {
        float text_w = dl->calc_text_size(text_.c_str(), font).x;
        tx = origin.x + width_ * zoom - text_w;
    }
    dl->add_text(Pt(tx, ty), text_.c_str(), color_, font);
}
