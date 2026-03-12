#include "visual/node/widget/containers/row.h"
#include <algorithm>

Widget* Row::addChild(std::unique_ptr<Widget> child) {
    auto* ptr = child.get();
    children_.push_back(std::move(child));
    return ptr;
}

Widget* Row::child(size_t i) const {
    return i < children_.size() ? children_[i].get() : nullptr;
}

Pt Row::getPreferredSize(IDrawList* dl) const {
    float sum_w = 0;
    float max_h = 0;
    for (const auto& c : children_) {
        Pt ps = c->getPreferredSize(dl);
        sum_w += ps.x;
        max_h = std::max(max_h, ps.y);
    }
    return Pt(sum_w, max_h);
}

void Row::layout(float available_width, float available_height) {
    width_ = available_width;
    height_ = available_height;

    float fixed_total = 0;
    int flex_count = 0;
    for (const auto& c : children_) {
        if (c->isFlexible()) {
            flex_count++;
        } else {
            fixed_total += c->getPreferredSize(nullptr).x;
        }
    }

    float remaining = std::max(0.0f, available_width - fixed_total);
    float flex_w = flex_count > 0 ? remaining / flex_count : 0;

    float x = 0;
    for (auto& c : children_) {
        float child_w = c->isFlexible() ? flex_w : c->getPreferredSize(nullptr).x;
        c->setPosition(x, 0);
        c->layout(child_w, available_height);
        x += child_w;
    }
}

void Row::render(IDrawList* dl, Pt origin, float zoom) const {
    for (const auto& c : children_) {
        Pt child_origin(origin.x + c->x() * zoom, origin.y + c->y() * zoom);
        c->render(dl, child_origin, zoom);
    }
}
