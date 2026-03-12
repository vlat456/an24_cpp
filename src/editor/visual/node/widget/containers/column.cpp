#include "visual/node/widget/containers/column.h"
#include <algorithm>

Widget* Column::addChild(std::unique_ptr<Widget> child) {
    auto* ptr = child.get();
    children_.push_back(std::move(child));
    return ptr;
}

Widget* Column::child(size_t i) const {
    return i < children_.size() ? children_[i].get() : nullptr;
}

Pt Column::getPreferredSize(IDrawList* dl) const {
    float max_w = 0;
    float sum_h = 0;
    for (const auto& c : children_) {
        Pt ps = c->getPreferredSize(dl);
        max_w = std::max(max_w, ps.x);
        sum_h += ps.y;
    }
    return Pt(max_w, sum_h);
}

void Column::layout(float available_width, float available_height) {
    width_ = available_width;
    height_ = available_height;

    float fixed_total = 0;
    int flex_count = 0;
    for (const auto& c : children_) {
        if (c->isFlexible()) {
            flex_count++;
        } else {
            fixed_total += c->getPreferredSize(nullptr).y;
        }
    }

    float remaining = std::max(0.0f, available_height - fixed_total);
    float flex_h = flex_count > 0 ? remaining / flex_count : 0;

    float y = 0;
    for (auto& c : children_) {
        float child_h = c->isFlexible() ? flex_h : c->getPreferredSize(nullptr).y;
        c->setPosition(0, y);
        c->layout(available_width, child_h);
        y += child_h;
    }
}

void Column::render(IDrawList* dl, Pt origin, float zoom) const {
    for (const auto& c : children_) {
        Pt child_origin(origin.x + c->x() * zoom, origin.y + c->y() * zoom);
        c->render(dl, child_origin, zoom);
    }
}
