#pragma once

#include "ui/math/pt.h"
#include "ui/renderer/idraw_list.h"
#include <string_view>
#include <vector>
#include <memory>
#include <cstdint>
#include <algorithm>

namespace ui {

/// Base widget with geometry and hierarchy.
/// Domain-specific widgets (e.g. visual::Widget) inherit from this.
class Widget {
public:
    virtual ~Widget() = default;
    
    virtual std::string_view id() const { return {}; }
    
    float zOrder() const { return z_order_; }
    void setZOrder(float z) { z_order_ = z; }
    
    Pt localPos() const { return local_pos_; }
    void setLocalPos(Pt p) { 
        local_pos_ = p; 
        onLocalPosChanged();
    }
    
    virtual void onLocalPosChanged() {}
    
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
    
    Widget* parent() const { return parent_; }
    
    void addChild(std::unique_ptr<Widget> child) {
        child->parent_ = this;
        children_.push_back(std::move(child));
    }

    std::unique_ptr<Widget> removeChild(Widget* child) {
        auto it = std::find_if(children_.begin(), children_.end(),
            [child](const auto& p) { return p.get() == child; });
        if (it == children_.end()) return nullptr;
        
        auto result = std::move(*it);
        children_.erase(it);
        result->parent_ = nullptr;
        return result;
    }
    
    template<typename T, typename... Args>
    T* emplaceChild(Args&&... args) {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = child.get();
        addChild(std::move(child));
        return ptr;
    }
    
    const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }
    
    virtual bool isClickable() const { return false; }
    virtual bool isResizable() const { return false; }
    
    bool isFlexible() const { return flexible_; }
    void setFlexible(bool f) { flexible_ = f; }
    
    virtual Pt preferredSize(IDrawList* dl) const { return size_; }
    virtual void layout(float w, float h) { size_ = Pt(w, h); }
    
    virtual void render(IDrawList* dl) const {}
    virtual void renderPost(IDrawList* dl) const {}
    
    void renderTree(IDrawList* dl) const {
        render(dl);
        for (const auto& c : children_) {
            c->renderTree(dl);
        }
        renderPost(dl);
    }
    
protected:
    Pt local_pos_{0, 0};
    Pt size_{0, 0};
    float z_order_ = 0.0f;
    bool flexible_ = false;
    
    Widget* parent_ = nullptr;
    std::vector<std::unique_ptr<Widget>> children_;
    
    std::vector<std::unique_ptr<Widget>>& mutableChildren() { return children_; }
};

} // namespace ui
