#include "scene.h"
#include "render_context.h"
#include <algorithm>

namespace visual {

ui::Widget* Scene::add(std::unique_ptr<ui::Widget> w) {
    auto* ptr = w.get();
    auto* vw = static_cast<Widget*>(ptr);
    indexWidget(ptr);
    propagateScene(ptr);

    // Insert at sorted position by renderLayer (stable: preserves insertion
    // order within the same layer). This keeps roots_ in draw order so
    // render() is a single linear pass.
    auto layer = vw->renderLayer();
    auto it = std::upper_bound(roots_.begin(), roots_.end(), layer,
        [](RenderLayer lyr, const std::unique_ptr<ui::Widget>& r) {
            return lyr < static_cast<Widget*>(r.get())->renderLayer();
        });
    roots_.insert(it, std::move(w));
    return ptr;
}

void Scene::render(IDrawList* dl, const RenderContext& ctx) {
    for (const auto& r : roots_) {
        static_cast<Widget*>(r.get())->renderTree(dl, ctx);
    }
}

void Scene::propagateScene(ui::Widget* w) {
    static_cast<Widget*>(w)->scene_ = this;
    ui::Scene::propagateScene(w);  // base handles grid insert
}

void Scene::detachScene(ui::Widget* w) {
    ui::Scene::detachScene(w);  // base handles grid remove
    static_cast<Widget*>(w)->scene_ = nullptr;
}

} // namespace visual
