#include "visual/node/widget/widget_base.h"
#include "data/node.h"

Pt Widget::getPreferredSize(IDrawList*) const {
    return Pt(width_, height_);
}

void Widget::layout(float available_width, float available_height) {
    width_ = available_width;
    height_ = available_height;
}

void Widget::updateFromContent(const NodeContent&) {
}
