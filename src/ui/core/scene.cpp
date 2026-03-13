#include "scene.h"
#include <algorithm>

namespace ui {

Widget* Scene::add(std::unique_ptr<Widget> w) {
    auto* ptr = w.get();
    propagateScene(ptr);
    auto z = w->zOrder();
    auto it = std::upper_bound(roots_.begin(), roots_.end(), z,
        [](float z_val, const std::unique_ptr<Widget>& r) {
            return z_val < r->zOrder();
        });
    roots_.insert(it, std::move(w));
    return ptr;
}

void Scene::remove(Widget* w) {
    pending_removals_.push_back(w);
}

void Scene::flushRemovals() {
    while (!pending_removals_.empty()) {
        auto batch = std::move(pending_removals_);
        pending_removals_.clear();
        
        for (Widget* w : batch) {
            auto it = std::find_if(roots_.begin(), roots_.end(),
                [w](const auto& p) { return p.get() == w; });
            if (it != roots_.end()) {
                detachScene(it->get());
                roots_.erase(it);
            }
        }
    }
}

void Scene::clear() {
    pending_removals_.clear();
    for (auto& r : roots_) detachScene(r.get());
    roots_.clear();
    grid_.clear();
}

Widget* Scene::find(std::string_view id) const {
    for (const auto& r : roots_) {
        if (r->id() == id) return r.get();
    }
    return nullptr;
}

void Scene::render(IDrawList* dl) {
    for (const auto& r : roots_) {
        r->renderTree(dl);
    }
}

void Scene::propagateScene(Widget* w) {
    w->scene_ = this;
    if (w->isClickable()) grid_.insert(w);
    for (auto& c : w->children()) propagateScene(c.get());
}

void Scene::detachScene(Widget* w) {
    if (w->isClickable()) grid_.remove(w);
    for (auto& c : w->children()) detachScene(c.get());
    w->scene_ = nullptr;
}

} // namespace ui
