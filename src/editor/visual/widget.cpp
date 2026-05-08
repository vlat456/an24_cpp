#include "widget.h"
#include "scene.h"
#include "render_context.h"
#include <functional>

namespace visual {

Widget::~Widget() {
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
    if (paint_enabled_) {
        render(dl, ctx);
    }
    for (const auto& c : children_) {
        static_cast<Widget*>(c.get())->renderTree(dl, ctx);
    }
    if (paint_enabled_ && ctx.show_debug_bounds && dl != nullptr) {
        Pt const min = ctx.world_to_screen(worldPos());
        Pt const max = ctx.world_to_screen(worldPos() + size());
        dl->add_rect(min, max, 0xFF0000FF, 1.0f);
    }
    if (paint_enabled_ && ctx.show_debug_paint_bounds && dl != nullptr) {
        renderDebugPaintBounds(dl, ctx);
    }
    if (paint_enabled_) {
        renderPost(dl, ctx);
    }
}

} // namespace visual
