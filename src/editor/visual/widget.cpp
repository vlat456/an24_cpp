#include "widget.h"
#include "scene.h"
#include "render_context.h"
#include <functional>

namespace visual {

Widget::~Widget() {
    // Ensure we're removed from the grid on destruction.
    if (scene_) {
        scene_->grid().remove(this);
    }
}

void Widget::updateGridRecursive(Widget* w) {
    if (w->isClickable()) scene_->grid().update(w);
    for (auto& c : w->children_) {
        updateGridRecursive(static_cast<Widget*>(c.get()));
    }
}

void Widget::onLocalPosChanged() {
    if (scene_) {
        updateGridRecursive(this);
    }
}

void Widget::renderTree(IDrawList* dl, const RenderContext& ctx) const {
    render(dl, ctx);
    for (const auto& c : children_) {
        static_cast<Widget*>(c.get())->renderTree(dl, ctx);
    }
    renderPost(dl, ctx);
}

} // namespace visual
