# Task: Make visual::Widget extend ui::Widget

## Objective
Refactor `visual::Widget` to inherit from `ui::Widget`, keeping domain-specific methods in the derived class.

## Changes

### 1. src/editor/visual/widget.h

Replace the entire file with:

```cpp
#pragma once
#include "ui/core/widget.h"
#include "visual/render_context.h"
#include <optional>
#include <cstdint>

struct NodeContent;

namespace visual {

class Port;

class Widget : public ui::Widget {
public:
    virtual ~Widget() = default;
    
    // Domain-specific methods (not in ui::Widget)
    
    /// Find a port child by name. For bus widgets, wire_id selects the alias port.
    virtual Port* portByName(std::string_view port_name,
                             std::string_view wire_id = {}) const {
        (void)port_name; (void)wire_id; return nullptr;
    }
    
    virtual void updateFromContent(const NodeContent& content) {}

    /// Custom fill color (nullopt = use theme default).
    virtual void setCustomColor(std::optional<uint32_t> c) { (void)c; }
    virtual std::optional<uint32_t> customColor() const { return std::nullopt; }
    
    /// Domain-specific render with RenderContext
    virtual void render(ui::IDrawList* dl, const RenderContext& ctx) const {}
    virtual void renderPost(ui::IDrawList* dl, const RenderContext& ctx) const {}
    void renderTree(ui::IDrawList* dl, const RenderContext& ctx) const;
};

} // namespace visual
```

### 2. src/editor/visual/widget.cpp

Replace the entire file with:

```cpp
#include "widget.h"
#include "scene.h"

namespace visual {

void Widget::renderTree(ui::IDrawList* dl, const RenderContext& ctx) const {
    render(dl, ctx);
    for (const auto& c : children()) {
        static_cast<const Widget*>(c.get())->renderTree(dl, ctx);
    }
    renderPost(dl, ctx);
}

} // namespace visual
```

### 3. Remove RenderLayer enum

The `RenderLayer` enum is replaced by `ui::Widget::zOrder()`:
- `RenderLayer::Group` → `zOrder() = 0.0f`
- `RenderLayer::Text` → `zOrder() = 1.0f`
- `RenderLayer::Wire` → `zOrder() = 2.0f`
- `RenderLayer::Normal` → `zOrder() = 3.0f` (default)

Search for `renderLayer()` usage and update:
```bash
grep -r "renderLayer\|RenderLayer" src/editor/
```

### 4. Update visual::Scene

In `src/editor/visual/scene.h`, change Scene to use composition with ui::Scene or inherit from it.

For now, keep visual::Scene as-is but update it to work with the new Widget hierarchy.

## Verification

After changes:
1. Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug`
2. Run: `cmake --build build -j8`
3. All existing tests should still pass.

## Notes

- This is a breaking change for code that uses `visual::Widget` directly
- The `ui::Widget` base handles: geometry, hierarchy, z-order, isClickable, isFlexible
- The `visual::Widget` adds: ports, custom colors, domain-specific render signature
