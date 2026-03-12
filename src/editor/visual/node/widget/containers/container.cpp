#include "visual/node/widget/containers/container.h"

Container::Container(std::unique_ptr<Widget> child, Edges margins)
    : child_(std::move(child)), margins_(margins) {}

Pt Container::getPreferredSize(IDrawList* dl) const {
    Pt child_ps = child_ ? child_->getPreferredSize(dl) : Pt(0, 0);
    return Pt(
        margins_.left + child_ps.x + margins_.right,
        margins_.top + child_ps.y + margins_.bottom
    );
}

void Container::layout(float available_width, float available_height) {
    width_ = available_width;
    height_ = available_height;

    if (child_) {
        float child_w = available_width - margins_.left - margins_.right;
        float child_h = available_height - margins_.top - margins_.bottom;
        child_->setPosition(margins_.left, margins_.top);
        child_->layout(child_w, child_h);
    }
}

void Container::render(IDrawList* dl, Pt origin, float zoom) const {
    if (child_) {
        Pt child_origin(origin.x + child_->x() * zoom, origin.y + child_->y() * zoom);
        child_->render(dl, child_origin, zoom);
    }
}
