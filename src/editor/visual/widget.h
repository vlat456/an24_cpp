#pragma once
#include "ui/math/pt.h"
#include "ui/renderer/idraw_list.h"
#include "ui/core/widget.h"
#include "visual/render_context.h"
#include <string_view>
#include <vector>
#include <memory>
#include <cassert>
#include <optional>
#include <cstdint>

struct NodeContent;

namespace visual {

using ui::Pt;
using ui::IDrawList;

class Scene;
class Port;

enum class InteractionRole : uint8_t {
    Toggle,
    DiscreteSelector,
    ContinuousScalar,
};

struct InteractionGeometry {
    Pt origin{0.0f, 0.0f};
    Pt size{0.0f, 0.0f};

    bool contains(Pt local_pos) const {
        return local_pos.x >= origin.x && local_pos.y >= origin.y
            && local_pos.x <= origin.x + size.x && local_pos.y <= origin.y + size.y;
    }
};

struct InteractionTarget {
    InteractionRole role = InteractionRole::Toggle;
    InteractionGeometry geometry{};
    float local_primary = 0.0f;
    
    // Mapping metadata for ContinuousScalar (slider) interaction:
    // local_primary is normalized to [primary_min, primary_max] for slider value computation.
    float primary_min = 0.0f;   ///< local_x coordinate where normalized value = 0
    float primary_max = 100.0f; ///< local_x coordinate where normalized value = 1
    
    // Discrete metadata for DiscreteSelector (knob) interaction:
    int steps = 2; ///< number of discrete positions (default 2 for toggle-like behavior)
};

/// Z-order layer for rendering. Lower values render first (further back).
enum class RenderLayer : uint8_t {
    Group  = 0,   ///< Behind everything (group containers)
    Text   = 1,   ///< Behind nodes (text annotations)
    Normal = 2,   ///< Component nodes, resize handles
    Wire   = 3    ///< Wires and arrowheads (topmost, on top of everything)
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
    virtual InteractionGeometry affordance_bounds_local() const {
        return InteractionGeometry{Pt(0.0f, 0.0f), size()};
    }
    virtual std::optional<InteractionTarget> interaction_target(Pt local_pos) const {
        (void)local_pos;
        return std::nullopt;
    }

    virtual void onLocalPosChanged() override;

    /// Custom fill color (nullopt = use theme default).
    /// Override in node widget subclasses that support custom coloring.
    virtual void setCustomColor(std::optional<uint32_t> c) { (void)c; }
    virtual std::optional<uint32_t> customColor() const { return std::nullopt; }

    // The context-free render path (inherited from ui::Widget) is not meaningful
    // for visual widgets. Override to catch accidental misuse in debug builds.
    // Visual widgets must be rendered via render(IDrawList*, const RenderContext&).
    void render(IDrawList*) const override {
        assert(false && "visual::Widget::render(IDrawList*) called without RenderContext — "
                        "use render(IDrawList*, const RenderContext&) instead");
    }

    virtual void render(IDrawList* dl, const RenderContext& ctx) const {}
    virtual void renderPost(IDrawList* dl, const RenderContext& ctx) const {}
    
    void renderTree(IDrawList* dl, const RenderContext& ctx) const;

    Widget* parent() const { return static_cast<Widget*>(ui::Widget::parent()); }
    Scene* scene() const { return scene_; }

    /// Walk up the parent chain to the root widget (one with no parent)
    /// and return its id(). Useful for mapping a port widget back to its
    /// owning node widget in the scene tree.
    std::string_view rootAncestorId() const {
        const Widget* cur = this;
        while (cur->parent()) cur = cur->parent();
        return cur->id();
    }

protected:
    friend class Scene;
    Scene* scene_ = nullptr;
    void updateGridRecursive(Widget* w);
};

} // namespace visual
