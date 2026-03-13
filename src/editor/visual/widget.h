#pragma once
#include "data/pt.h"
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>

struct NodeContent;

struct IDrawList;

namespace visual {

class Scene;
class Port;
struct RenderContext;

/// Z-order layer for rendering. Lower values render first (further back).
enum class RenderLayer : uint8_t {
    Group  = 0,   ///< Behind everything (group containers)
    Text   = 1,   ///< Behind wires/nodes (text annotations)
    Wire   = 2,   ///< Between text and nodes
    Normal = 3    ///< Default (component nodes, resize handles)
};

class Widget {
public:
    virtual ~Widget();
    
    virtual std::string_view id() const { return {}; }
    
    /// Render layer for Z-ordering. Override in subclasses to change draw order.
    virtual RenderLayer renderLayer() const { return RenderLayer::Normal; }
    
    Pt localPos() const { return local_pos_; }
    void setLocalPos(Pt p);
    
    Pt size() const { return size_; }
    void setSize(Pt s) { size_ = s; }
    
    Pt worldPos() const {
        return parent_ ? parent_->worldPos() + local_pos_ : local_pos_;
    }
    virtual Pt worldMin() const { return worldPos(); }
    virtual Pt worldMax() const { return worldPos() + size_; }
    
    bool contains(Pt world_p) const {
        Pt mn = worldMin(), mx = worldMax();
        return world_p.x >= mn.x && world_p.x <= mx.x &&
               world_p.y >= mn.y && world_p.y <= mx.y;
    }
    
    Scene* scene() const { return scene_; }
    Widget* parent() const { return parent_; }
    
    void addChild(std::unique_ptr<Widget> child);
    std::unique_ptr<Widget> removeChild(Widget* child);
    
    template<typename T, typename... Args>
    T* emplaceChild(Args&&... args) {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = child.get();
        addChild(std::move(child));
        return ptr;
    }
    
    const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }
    
    virtual bool isClickable() const { return false; }
    
    /// Whether the widget supports resizing (group nodes, text annotations).
    virtual bool isResizable() const { return false; }
    
    /// Find a port child by name. For bus widgets, wire_id selects the alias port.
    /// Default returns nullptr (widget has no ports).
    virtual Port* portByName(std::string_view port_name,
                             std::string_view wire_id = {}) const {
        (void)port_name; (void)wire_id; return nullptr;
    }
    
    bool isFlexible() const { return flexible_; }
    void setFlexible(bool f) { flexible_ = f; }
    
    virtual void updateFromContent(const NodeContent& content) {}

    /// Custom fill color (nullopt = use theme default).
    /// Override in node widget subclasses that support custom coloring.
    virtual void setCustomColor(std::optional<uint32_t> c) { (void)c; }
    virtual std::optional<uint32_t> customColor() const { return std::nullopt; }
    
    virtual Pt preferredSize(IDrawList* dl) const { return size_; }
    virtual void layout(float w, float h) { size_ = Pt(w, h); }
    
    virtual void render(IDrawList* dl, const RenderContext& ctx) const {}
    /// Post-render hook: called after all children have been rendered.
    /// Use this to draw overlays (selection borders, resize handles) that
    /// must appear on top of child content.
    virtual void renderPost(IDrawList* dl, const RenderContext& ctx) const {}
    void renderTree(IDrawList* dl, const RenderContext& ctx) const;

    void propagateSceneToChildren(Scene* s);
    void detachSceneFromChildren();

protected:
    Pt local_pos_{0, 0};
    Pt size_{0, 0};
    bool flexible_ = false;
    
    Scene* scene_ = nullptr;
    Widget* parent_ = nullptr;
    std::vector<std::unique_ptr<Widget>> children_;
    
    friend class Scene;
    
    void updateGridRecursive(Widget* w);
};

} // namespace visual
