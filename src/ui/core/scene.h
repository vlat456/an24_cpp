#pragma once

#include "widget.h"
#include "grid.h"
#include "ui/renderer/idraw_list.h"
#include <memory>
#include <vector>
#include <algorithm>
#include <string_view>

namespace ui {

/// Generic scene that owns and manages a tree of widgets.
/// Template parameter WidgetType must inherit from ui::Widget<SceneType>
/// and expose a `using SceneType = ...` alias.
template<typename WidgetType>
class Scene {
public:
    Scene() = default;
    virtual ~Scene() = default;
    
    // Non-copyable (owns unique_ptrs).
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = default;
    Scene& operator=(Scene&&) = default;
    
    Grid& grid() { return grid_; }
    const Grid& grid() const { return grid_; }
    
    virtual WidgetType* add(std::unique_ptr<WidgetType> w) {
        auto* ptr = w.get();
        propagateScene(ptr);
        auto z = w->zOrder();
        auto it = std::upper_bound(roots_.begin(), roots_.end(), z,
            [](float z_val, const std::unique_ptr<WidgetType>& r) {
                return z_val < r->zOrder();
            });
        roots_.insert(it, std::move(w));
        return ptr;
    }
    
    void remove(WidgetType* w) {
        pending_removals_.push_back(w);
    }
    
    void flushRemovals() {
        while (!pending_removals_.empty()) {
            auto batch = std::move(pending_removals_);
            pending_removals_.clear();
            
            for (WidgetType* w : batch) {
                auto it = std::find_if(roots_.begin(), roots_.end(),
                    [w](const auto& p) { return p.get() == w; });
                if (it != roots_.end()) {
                    detachScene(it->get());
                    roots_.erase(it);
                }
            }
        }
    }
    
    void clear() {
        pending_removals_.clear();
        for (auto& r : roots_) detachScene(r.get());
        roots_.clear();
        grid_.clear();
    }
    
    const std::vector<std::unique_ptr<WidgetType>>& roots() const { return roots_; }
    
    WidgetType* find(std::string_view id) const {
        for (const auto& r : roots_) {
            if (r->id() == id) return r.get();
        }
        return nullptr;
    }
    
    void render(IDrawList* dl) {
        for (const auto& r : roots_) {
            r->renderTree(dl);
        }
    }

    /// Attach a non-owned widget (and its children) to this scene.
    /// Sets the scene_ pointer and registers clickable widgets in the grid.
    /// Useful when the widget is owned externally (e.g. by a parent widget).
    void attachToScene(WidgetType* w) { propagateScene(w); }

protected:
    Grid grid_;
    std::vector<std::unique_ptr<WidgetType>> roots_;
    std::vector<WidgetType*> pending_removals_;
    
    void propagateScene(WidgetType* w) {
        if (w->isClickable()) grid_.insert(w);
        for (auto& c : w->children()) {
            propagateScene(static_cast<WidgetType*>(c.get()));
        }
    }
    
    void detachScene(WidgetType* w) {
        if (w->isClickable()) grid_.remove(w);
        for (auto& c : w->children()) {
            detachScene(static_cast<WidgetType*>(c.get()));
        }
    }
};

/// Concrete scene for the default pure-UI widget type.
class BaseScene : public Scene<BaseWidget> {};

} // namespace ui
