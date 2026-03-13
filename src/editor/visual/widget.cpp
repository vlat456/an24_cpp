#include "widget.h"
#include "scene.h"
#include "render_context.h"
#include <functional>

namespace visual {

Widget::~Widget() {
    // Safety net: if this widget is still registered in a scene's grid,
    // remove it to prevent dangling pointers.
    // Note: we cannot use isClickable() here because virtual dispatch
    // in destructors resolves to the base class. Instead, unconditionally
    // ask the grid to remove us — Grid::remove is a no-op if not found.
    if (scene_) {
        scene_->grid().remove(this);
    }
}

void Widget::setLocalPos(Pt p) {
    local_pos_ = p;
    if (scene_) {
        updateGridRecursive(this);
    }
}

void Widget::updateGridRecursive(Widget* w) {
    if (w->isClickable()) scene_->grid().update(w);
    for (auto& c : w->children_) {
        updateGridRecursive(c.get());
    }
}

void Widget::propagateSceneToChildren(Scene* s) {
    if (scene_ == s) return;
    scene_ = s;
    if (scene_ && isClickable()) {
        scene_->grid().insert(this);
    }
    for (auto& c : children_) {
        c->propagateSceneToChildren(s);
    }
}

void Widget::detachSceneFromChildren() {
    if (scene_ && isClickable()) {
        scene_->grid().remove(this);
    }
    for (auto& c : children_) {
        c->detachSceneFromChildren();
    }
    scene_ = nullptr;
}

void Widget::addChild(std::unique_ptr<Widget> child) {
    child->parent_ = this;
    if (scene_) {
        child->propagateSceneToChildren(scene_);
    }
    children_.push_back(std::move(child));
}

std::unique_ptr<Widget> Widget::removeChild(Widget* child) {
    auto it = std::find_if(children_.begin(), children_.end(),
        [child](const auto& p) { return p.get() == child; });
    if (it == children_.end()) return nullptr;
    
    child->detachSceneFromChildren();
    auto result = std::move(*it);
    children_.erase(it);
    result->parent_ = nullptr;
    return result;
}

void Widget::renderTree(IDrawList* dl, const RenderContext& ctx) const {
    render(dl, ctx);
    for (const auto& c : children_) {
        c->renderTree(dl, ctx);
    }
    renderPost(dl, ctx);
}

} // namespace visual
