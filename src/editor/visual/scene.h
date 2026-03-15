#pragma once
#include "widget.h"
#include "ui/core/scene.h"
#include <memory>
#include <vector>

namespace visual {

struct RenderContext;

/// Editor scene inheriting generic ui::Scene behaviour.
/// Overrides add() to sort by RenderLayer instead of numeric z-order,
/// and provides a render() overload that passes RenderContext.
class Scene : public ui::Scene {
public:
    Scene() = default;
    
    /// Insert a widget sorted by RenderLayer (stable within same layer).
    /// All widgets added to a visual::Scene must be visual::Widget subclasses.
    ui::Widget* add(std::unique_ptr<ui::Widget> w) override;
    
    /// Type-safe find that returns visual::Widget*.
    Widget* find(std::string_view id) const {
        return static_cast<Widget*>(ui::Scene::find(id));
    }
    
    /// Render all root widgets with domain-specific RenderContext.
    void render(IDrawList* dl, const RenderContext& ctx);

protected:
    void propagateScene(ui::Widget* w) override;
    void detachScene(ui::Widget* w) override;
};

} // namespace visual
