#pragma once
#include "ui/math/pt.h"
#include "ui/renderer/idraw_list.h"
#include "ui/core/widget.h"
#include "visual/render_context.h"
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>

struct NodeContent;

namespace visual {

using ui::Pt;
using ui::IDrawList;

class Scene;
class Port;

/// Z-order layer for rendering. Lower values render first (further back).
enum class RenderLayer : uint8_t {
    Group  = 0,   ///< Behind everything (group containers)
    Text   = 1,   ///< Behind wires/nodes (text annotations)
    Wire   = 2,   ///< Between text and nodes
    Normal = 3    ///< Default (component nodes, resize handles)
};

class Widget : public ui::Widget {
public:
    virtual ~Widget();
    
    /// Render layer for Z-ordering. Override in subclasses to change draw order.
    virtual RenderLayer renderLayer() const { return RenderLayer::Normal; }
    
    /// Find a port child by name. For bus widgets, wire_id selects the alias port.
    /// Default returns nullptr (widget has no ports).
    virtual Port* portByName(std::string_view port_name,
                             std::string_view wire_id = {}) const {
        (void)port_name; (void)wire_id; return nullptr;
    }
    
    virtual void updateFromContent(const NodeContent& content) {}

    virtual void onLocalPosChanged() override;

    /// Custom fill color (nullopt = use theme default).
    /// Override in node widget subclasses that support custom coloring.
    virtual void setCustomColor(std::optional<uint32_t> c) { (void)c; }
    virtual std::optional<uint32_t> customColor() const { return std::nullopt; }

    // Note: render(IDrawList*) and renderTree(IDrawList*) are inherited from ui::Widget
    // but are visual-specific. Visual widgets should be rendered via render(IDrawList*, const RenderContext&)
    // which is called by visual::Scene::render(IDrawList*, const RenderContext&).
    // The IDrawList*-only methods exist for interface compatibility with the generic ui::Scene.
    virtual void render(IDrawList* dl, const RenderContext& ctx) const {}
    virtual void renderPost(IDrawList* dl, const RenderContext& ctx) const {}
    
    void renderTree(IDrawList* dl, const RenderContext& ctx) const;

    Widget* parent() const { return static_cast<Widget*>(ui::Widget::parent()); }
    Scene* scene() const { return scene_; }

protected:
    friend class Scene;
    Scene* scene_ = nullptr;
    void updateGridRecursive(Widget* w);
};

} // namespace visual
