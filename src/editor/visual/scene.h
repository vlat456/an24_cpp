#pragma once

#include "widget.h"
#include "ui/core/scene.h"
#include <iterator>
#include <memory>
#include <vector>

namespace visual {

class ISpriteCache;
struct RenderContext;

// ==-----------------------------------------------------------------------
// Typed view over visual::Scene roots.
//
// Zero-allocation, C++17-compatible iterator adaptor.
// Centralises the ui::Widget* → visual::Widget* downcast in ONE place.
// Invariant: visual::Scene::add() only accepts visual::Widget, so every
// root is guaranteed to be a visual::Widget.
//
// Usage:
//   for (auto* w : scene.visual_roots()) { ... }   // w deduces as Widget*
//   for (auto  w : scene.visual_roots()) { ... }   // same, implicit copy
// ==-----------------------------------------------------------------------

class VisualRootView {
public:
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = Widget*;
        using difference_type   = std::ptrdiff_t;
        using pointer           = Widget*;   // matches operator*() prvalue return
        using reference         = Widget*;   // prvalue — legal per C++20 indirectly_readable

        Iterator() = default;  // full forward_iterator conformance (std::regular)

        explicit Iterator(std::vector<std::unique_ptr<ui::Widget>>::const_iterator it)
            : it_(it) {}

        /// Dereference: performs the enforced downcast from base to visual::Widget.
        Widget* operator*() const {
            return static_cast<Widget*>(it_->get());
        }

        Iterator& operator++() { ++it_; return *this; }
        Iterator  operator++(int) { auto tmp = *this; ++it_; return tmp; }
        bool      operator==(const Iterator& o) const { return it_ == o.it_; }
        bool      operator!=(const Iterator& o) const { return it_ != o.it_; }

    private:
        std::vector<std::unique_ptr<ui::Widget>>::const_iterator it_;
    };

    explicit VisualRootView(const std::vector<std::unique_ptr<ui::Widget>>& roots)
        : roots_(roots) {}

    Iterator begin() const { return Iterator(roots_.begin()); }
    Iterator end()   const { return Iterator(roots_.end()); }
    bool     empty() const { return roots_.empty(); }
    size_t   size()  const { return roots_.size(); }

private:
    const std::vector<std::unique_ptr<ui::Widget>>& roots_;
};

// ==-----------------------------------------------------------------------
// visual::Scene — typed scene that owns visual::Widget roots.
// ==-----------------------------------------------------------------------

class Scene : public ui::Scene {
public:
    Scene() = default;

    /// Override add() to accept only visual::Widget (enforced by parameter
    /// type in caller code — the base accepts ui::Widget but visual callers
    /// always pass visual::Widget, and we static_cast accordingly).
    ui::Widget* add(std::unique_ptr<ui::Widget> w) override;

    void flushRemovals() override {
        ui::Scene::flushRemovals();
        crossings_dirty_ = true;
    }

    void clear() override {
        ui::Scene::clear();
        crossings_dirty_ = true;
    }

    /// Typed lookup — all widgets in this scene are visual::Widget.
    Widget* find(std::string_view id) const {
        return static_cast<Widget*>(ui::Scene::find(id));
    }

    /// Incremental mutation: mark a node widget for removal.
    /// Evicts the node's sprite cache entry if a cache is provided.
    /// Call flushRemovals() (or use flushGuard()) to complete the removal.
    void remove_node(std::string_view node_id, ISpriteCache* cache = nullptr);

    /// Incremental mutation: mark a wire widget for removal.
    /// Call flushRemovals() (or use flushGuard()) to complete the removal.
    void remove_wire(std::string_view wire_id);

    /// Typed view over roots — zero-allocation, range-for compatible.
    /// Usage: `for (auto* w : scene.visual_roots()) { ... }`
    VisualRootView visual_roots() const {
        return VisualRootView(roots_);
    }

    void render(IDrawList* dl, const RenderContext& ctx);

    bool crossings_dirty() const { return crossings_dirty_; }
    void mark_crossings_dirty()  { crossings_dirty_ = true; }
    void clear_crossings_dirty() { crossings_dirty_ = false; }

protected:
    void propagateScene(ui::Widget* w) override;
    void detachScene(ui::Widget* w) override;

private:
    bool crossings_dirty_ = true;
};

} // namespace visual
