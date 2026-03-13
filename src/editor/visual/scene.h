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
class Scene : public ui::Scene<Widget> {
public:
    Scene() = default;
    
    /// Insert a widget sorted by RenderLayer (stable within same layer).
    Widget* add(std::unique_ptr<Widget> w) override;
    
    /// Render all root widgets with domain-specific RenderContext.
    void render(IDrawList* dl, const RenderContext& ctx);
};

} // namespace visual
