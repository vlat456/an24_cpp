#include "scene.h"
#include "render_context.h"
#include <algorithm>

#ifdef AN24_EDITOR
#include "visual/node_sprite_cache.h"
#include <imgui.h>
#endif

#ifdef AN24_PROFILE
#include <chrono>

namespace {
static constexpr int SCENE_REP = 120;
static const char* scene_names[] = {"scene: groups", "scene: text", "scene: nodes", "scene: wires"};
static double scene_accum[4] = {};
static int scene_count = 0;
static int sprite_hits_accum = 0;
static int sprite_misses_accum = 0;

void scene_report() {
    if (scene_count < SCENE_REP) return;
    double total = 0;
    for (int i = 0; i < 4; ++i) total += scene_accum[i];
    std::printf("\n=== Scene Render Breakdown (avg over %d frames, %.1f ms total) ===\n",
                scene_count, (total / scene_count) / 1000.0);
    for (int i = 0; i < 4; ++i) {
        double avg = scene_accum[i] / scene_count;
        double pct = total > 0 ? (scene_accum[i] / total) * 100.0 : 0;
        std::printf("  %-45s %8.1f us  (%5.1f%%)\n", scene_names[i], avg, pct);
    }
    std::printf("  sprite cache: %d hits, %d misses (per frame)\n",
                sprite_hits_accum / scene_count, sprite_misses_accum / scene_count);
    std::fflush(stdout);
    for (int i = 0; i < 4; ++i) scene_accum[i] = 0;
    sprite_hits_accum = 0;
    sprite_misses_accum = 0;
    scene_count = 0;
}
}
#endif

namespace visual {

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
#ifdef AN24_PROFILE
    double layer_us[4] = {};
    int frame_hits = 0, frame_misses = 0;
#endif

#ifdef AN24_EDITOR
    ImDrawList* raw_dl = static_cast<ImDrawList*>(dl->native_draw_list());
    NodeSpriteCache* cache = ctx.sprite_cache;
#endif

    for (const auto& r : roots_) {
        auto* vw = static_cast<Widget*>(r.get());

#ifdef AN24_PROFILE
        auto t0 = std::chrono::steady_clock::now();
#endif

#ifdef AN24_EDITOR
        // Sprite cache path: blit cached node, then render selection overlay.
        if (cache && vw->isClickable() && raw_dl) {
            if (vw->is_node_kind()) {
                auto* node = static_cast<Widget*>(vw);
                if (cache->has(node->id())) {
                    cache->blit(*node, raw_dl, ctx);
                    node->renderPost(dl, ctx);
#ifdef AN24_PROFILE
                    auto t1 = std::chrono::steady_clock::now();
                    int li = static_cast<int>(vw->renderLayer());
                    if (li >= 0 && li < 4) layer_us[li] += std::chrono::duration<double, std::micro>(t1 - t0).count();
                    ++frame_hits;
#endif
                    continue;
                }
#ifdef AN24_PROFILE
                ++frame_misses;
#endif
            }
        }
#endif

        vw->renderTree(dl, ctx);

#ifdef AN24_PROFILE
        auto t1 = std::chrono::steady_clock::now();
        int li = static_cast<int>(vw->renderLayer());
        if (li >= 0 && li < 4) layer_us[li] += std::chrono::duration<double, std::micro>(t1 - t0).count();
#endif
    }

#ifdef AN24_PROFILE
    for (int i = 0; i < 4; ++i) scene_accum[i] += layer_us[i];
    sprite_hits_accum += frame_hits;
    sprite_misses_accum += frame_misses;
    ++scene_count;
    scene_report();
#endif
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
