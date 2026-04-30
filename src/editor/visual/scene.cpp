#include "scene.h"
#include "render_context.h"
#include "visual/sprite_cache.h"
#include "editor/app/frame_profiler.h"
#include <algorithm>

namespace visual {

/// Static profiler for scene layer breakdown. Lazy-initialized on first use.
static an24::FrameProfiler& scene_profiler() {
    static an24::FrameProfiler p;
    static bool init = false;
    if (!init) {
        init = true;
        (void)p.register_section("scene: groups");
        (void)p.register_section("scene: text");
        (void)p.register_section("scene: nodes");
        (void)p.register_section("scene: wires");
    }
    return p;
}

ui::Widget* Scene::add(std::unique_ptr<ui::Widget> w) {
    auto* ptr = w.get();
    auto* vw = static_cast<Widget*>(ptr);
    indexWidget(ptr);
    propagateScene(ptr);

    auto layer = vw->renderLayer();
    auto it = std::upper_bound(roots_.begin(), roots_.end(), layer,
        [](RenderLayer lyr, const std::unique_ptr<ui::Widget>& r) {
            return lyr < static_cast<Widget*>(r.get())->renderLayer();
        });
    roots_.insert(it, std::move(w));
    crossings_dirty_ = true;
    return ptr;
}

void Scene::render(IDrawList* dl, const RenderContext& ctx) {
    auto& prof = scene_profiler();
    ISpriteCache* cache = ctx.sprite_cache;

    for (const auto& r : roots_) {
        auto* vw = static_cast<Widget*>(r.get());
        int li = static_cast<int>(vw->renderLayer());

        an24::ScopedSection sec(prof, li);

        // Sprite cache path: blit cached node, then render selection overlay.
        if (cache && vw->isClickable() && vw->is_node_kind()) {
            if (cache->has(vw->id())) {
                cache->blit(*vw, dl, ctx);
                vw->renderPost(dl, ctx);
                continue;
            }
        }

        vw->renderTree(dl, ctx);
    }

    prof.add_frame(0.0);
    prof.maybe_report();
}

void Scene::propagateScene(ui::Widget* w) {
    static_cast<Widget*>(w)->scene_ = this;
    ui::Scene::propagateScene(w);
}

void Scene::detachScene(ui::Widget* w) {
    ui::Scene::detachScene(w);
    static_cast<Widget*>(w)->scene_ = nullptr;
}

} // namespace visual
