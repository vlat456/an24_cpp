#include "widget.h"

namespace ui {

Widget::~Widget() {}

void Widget::setLocalPos(Pt p) {
    local_pos_ = p;
}

void Widget::addChild(std::unique_ptr<Widget> child) {
    child->parent_ = this;
    children_.push_back(std::move(child));
}

std::unique_ptr<Widget> Widget::removeChild(Widget* child) {
    auto it = std::find_if(children_.begin(), children_.end(),
        [child](const auto& p) { return p.get() == child; });
    if (it == children_.end()) return nullptr;
    
    auto result = std::move(*it);
    children_.erase(it);
    result->parent_ = nullptr;
    return result;
}

void Widget::renderTree(IDrawList* dl) const {
    render(dl);
    for (const auto& c : children_) {
        c->renderTree(dl);
    }
    renderPost(dl);
}

} // namespace ui
