#include "visual/node/layout.h"
#include "visual/renderer/draw_list.h"
#include <algorithm>
#include <cstring>

// ============================================================================
// Column
// ============================================================================

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

    // Phase 1: sum fixed heights, count flexible
    float fixed_total = 0;
    int flex_count = 0;
    for (const auto& c : children_) {
        if (c->isFlexible()) {
            flex_count++;
        } else {
            fixed_total += c->getPreferredSize(nullptr).y;
        }
    }

    // Phase 2: distribute remaining to flexible
    float remaining = std::max(0.0f, available_height - fixed_total);
    float flex_h = flex_count > 0 ? remaining / flex_count : 0;

    // Phase 3: position top-to-bottom
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

// ============================================================================
// Row
// ============================================================================

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

    // Phase 1: sum fixed widths, count flexible
    float fixed_total = 0;
    int flex_count = 0;
    for (const auto& c : children_) {
        if (c->isFlexible()) {
            flex_count++;
        } else {
            fixed_total += c->getPreferredSize(nullptr).x;
        }
    }

    // Phase 2: distribute remaining to flexible
    float remaining = std::max(0.0f, available_width - fixed_total);
    float flex_w = flex_count > 0 ? remaining / flex_count : 0;

    // Phase 3: position left-to-right
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

// ============================================================================
// Container
// ============================================================================

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

// ============================================================================
// Label
// ============================================================================

Label::Label(const std::string& text, float font_size, uint32_t color)
    : text_(text), font_size_(font_size), color_(color) {}

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
    dl->add_text(Pt(origin.x, ty), text_.c_str(), color_, font);
}

// ============================================================================
// Circle
// ============================================================================

Circle::Circle(float radius, uint32_t color)
    : radius_(radius), color_(color) {}

Pt Circle::getPreferredSize(IDrawList*) const {
    return Pt(radius_ * 2, radius_ * 2);
}

void Circle::render(IDrawList* dl, Pt origin, float zoom) const {
    if (!dl) return;
    float r = radius_ * zoom;
    Pt center(origin.x + width_ * zoom / 2, origin.y + height_ * zoom / 2);
    dl->add_circle_filled(center, r, color_);
}

// ============================================================================
// Spacer
// ============================================================================

Spacer::Spacer() {
    flexible_ = true;
}

Pt Spacer::getPreferredSize(IDrawList*) const {
    return Pt(0, 0);
}

void Spacer::render(IDrawList*, Pt, float) const {
    // intentionally empty
}
