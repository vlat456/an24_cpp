#include "scene.h"
#include "render_context.h"
#include <algorithm>

namespace visual {

Widget* Scene::add(std::unique_ptr<Widget> w) {
    auto* ptr = w.get();
    propagateScene(ptr);

    // Insert at sorted position by renderLayer (stable: preserves insertion
    // order within the same layer). This keeps roots_ in draw order so
    // render() is a single linear pass.
    auto layer = w->renderLayer();
    auto it = std::upper_bound(roots_.begin(), roots_.end(), layer,
        [](RenderLayer lyr, const std::unique_ptr<Widget>& r) {
            return lyr < r->renderLayer();
        });
    roots_.insert(it, std::move(w));
    return ptr;
}

void Scene::render(IDrawList* dl, const RenderContext& ctx) {
    for (const auto& r : roots_) {
        r->renderTree(dl, ctx);
    }
}

} // namespace visual
