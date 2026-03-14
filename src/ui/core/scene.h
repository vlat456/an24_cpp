#pragma once

#include "widget.h"
#include "grid.h"
#include "ui/renderer/idraw_list.h"
#include <memory>
#include <vector>
#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <cassert>

namespace ui {

/// Concrete scene that owns and manages a tree of widgets.
/// Maintains an O(1) id-to-widget index for fast lookups.
/// Subclasses can override propagateScene/detachScene for custom attach/detach behavior.
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
    
    virtual Widget* add(std::unique_ptr<Widget> w) {
        auto* ptr = w.get();
        indexWidget(ptr);
        propagateScene(ptr);
        auto z = w->zOrder();
        auto it = std::upper_bound(roots_.begin(), roots_.end(), z,
            [](float z_val, const std::unique_ptr<Widget>& r) {
                return z_val < r->zOrder();
            });
        roots_.insert(it, std::move(w));
        return ptr;
    }
    
    void remove(Widget* w) {
        pending_removals_.push_back(w);
    }
    
    void flushRemovals() {
        while (!pending_removals_.empty()) {
            auto batch = std::move(pending_removals_);
            pending_removals_.clear();
            
            for (Widget* w : batch) {
                auto it = std::find_if(roots_.begin(), roots_.end(),
                    [w](const auto& p) { return p.get() == w; });
                if (it != roots_.end()) {
                    unindexWidget(it->get());
                    detachScene(it->get());
                    roots_.erase(it);
                }
            }
        }
    }
    
    void clear() {
        pending_removals_.clear();
        for (auto& r : roots_) {
            unindexWidget(r.get());
            detachScene(r.get());
        }
        roots_.clear();
        grid_.clear();
        id_index_.clear();
    }
    
    const std::vector<std::unique_ptr<Widget>>& roots() const { return roots_; }
    
    /// O(1) lookup by widget ID.
    Widget* find(std::string_view id) const {
        auto it = id_index_.find(id);
        return (it != id_index_.end()) ? it->second : nullptr;
    }
    
    void render(IDrawList* dl) {
        for (const auto& r : roots_) {
            r->renderTree(dl);
        }
    }

    /// Attach a non-owned widget (and its children) to this scene.
    /// Sets the scene_ pointer and registers clickable widgets in the grid.
    /// Useful when the widget is owned externally (e.g. by a parent widget).
    void attachToScene(Widget* w) {
        indexWidget(w);
        propagateScene(w);
    }

    /// Detach a non-owned widget (and its children) from this scene.
    /// Removes from the grid and id index. Symmetric with attachToScene().
    /// Call before destroying a child widget that was previously attached.
    void detachFromScene(Widget* w) {
        unindexWidget(w);
        detachScene(w);
    }

    /// RAII guard that calls flushRemovals() on destruction.
    /// Ensures pending removals are processed even on early return or exception.
    /// 
    /// Usage:
    ///   {
    ///       auto guard = scene.flushGuard();
    ///       scene.remove(widget_a);
    ///       scene.remove(widget_b);
    ///       // ~FlushGuard() automatically calls flushRemovals()
    ///   }
    ///
    class FlushGuard {
    public:
        explicit FlushGuard(Scene& scene) : scene_(&scene) {}
        ~FlushGuard() { if (scene_) scene_->flushRemovals(); }
        
        FlushGuard(const FlushGuard&) = delete;
        FlushGuard& operator=(const FlushGuard&) = delete;
        
        FlushGuard(FlushGuard&& other) noexcept : scene_(other.scene_) {
            other.scene_ = nullptr;
        }
        FlushGuard& operator=(FlushGuard&&) = delete;
        
        /// Cancel automatic flush. Call if you want to control timing manually.
        void release() { scene_ = nullptr; }
        
    private:
        Scene* scene_;
    };
    
    /// Create a FlushGuard for this scene.
    FlushGuard flushGuard() { return FlushGuard(*this); }

protected:
    Grid grid_;
    std::vector<std::unique_ptr<Widget>> roots_;
    std::vector<Widget*> pending_removals_;
    
    /// O(1) id → widget index. Keys are string_views into each widget's
    /// id() return value, so they remain valid as long as the widget lives.
    mutable std::unordered_map<std::string_view, Widget*> id_index_;
    
    /// Virtual hook for attaching a widget to this scene.
    /// Override in derived scenes to set widget->scene_ pointer.
    virtual void propagateScene(Widget* w) {
        if (w->isClickable()) grid_.insert(w);
        for (auto& c : w->children()) {
            propagateScene(c.get());
        }
    }
    
    /// Virtual hook for detaching a widget from this scene.
    /// Override in derived scenes to clear widget->scene_ pointer.
    virtual void detachScene(Widget* w) {
        if (w->isClickable()) grid_.remove(w);
        for (auto& c : w->children()) {
            detachScene(c.get());
        }
    }

    /// Add a widget (and its children) to the id index.
    /// Respects isIndexable(): widgets that return false are skipped
    /// to avoid shadowing root-level widgets with the same ID.
    void indexWidget(Widget* w) {
        if (w->isIndexable()) {
            auto wid = w->id();
            if (!wid.empty()) {
                id_index_[wid] = w;
            }
        }
        for (auto& c : w->children()) {
            indexWidget(c.get());
        }
    }
    
    /// Remove a widget (and its children) from the id index.
    /// Only erases entries for widgets that are indexable, mirroring indexWidget.
    void unindexWidget(Widget* w) {
        if (w->isIndexable()) {
            auto wid = w->id();
            if (!wid.empty()) {
                id_index_.erase(wid);
            }
        }
        for (auto& c : w->children()) {
            unindexWidget(c.get());
        }
    }
};

} // namespace ui
