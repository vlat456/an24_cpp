# UI Framework

The project includes a lightweight UI framework under `src/ui/` used by the editor for widget trees, layouts, and rendering.

## Overview

```
┌─────────────────────────────────────────────┐
│ Scene                                        │
│  ├─ Grid (spatial indexing)                  │
│  └─ Widget[] (root widgets, z-ordered)       │
│       ├─ Widget (hierarchical children)      │
│       │    ├─ Layout data (LinearLayout)     │
│       │    └─ Render data (IDrawList)        │
│       └─ ...                                 │
└─────────────────────────────────────────────┘
```

## Core Classes

### Scene
Owns and manages a tree of widgets with O(1) id-to-widget indexing:
```cpp
class Scene {
    Grid& grid();
    Widget* add(std::unique_ptr<Widget> w);
    void remove(Widget* w);
    void flushRemovals();
    Widget* findById(std::string_view id) const;
    void render(IDrawList& draw_list);
};
```

File: `src/ui/core/scene.h`

### Widget
Base widget class with hierarchical children, layout, and rendering:
```cpp
class Widget {
    std::string id_;
    std::vector<std::unique_ptr<Widget>> children_;
    Widget* parent_ = nullptr;
    Scene* scene_ = nullptr;

    Rect bounds_;
    float z_order_ = 0.0f;
    bool visible_ = true;

public:
    const std::string& id() const;
    Widget* parent() const;
    Scene* scene() const;
    const Rect& bounds() const;
    void setBounds(const Rect& r);

    virtual void onAttach(Scene* s);
    virtual void onDetach();
    virtual void render(IDrawList& draw_list);
    virtual void layout();

    Widget* addChild(std::unique_ptr<Widget> child);
    void removeChild(Widget* child);
    Widget* findChild(std::string_view id) const;
};
```

File: `src/ui/core/widget.h`

### Grid
Spatial indexing for hit-testing and region queries:
```cpp
class Grid {
public:
    void insert(Widget* w);
    void remove(Widget* w);
    void update(Widget* w);
    std::vector<Widget*> query(const Rect& region) const;
    Widget* hitTest(const Pt& point) const;
};
```

File: `src/ui/core/grid.h`

## Layout

### LinearLayout
```cpp
class LinearLayout {
public:
    enum class Direction { Horizontal, Vertical };

    void setDirection(Direction d);
    void setSpacing(float spacing);
    void setPadding(Edges padding);
    void arrange(std::vector<Widget*>& widgets, const Rect& container);
};
```

File: `src/ui/layout/linear_layout.h`

### Edges
```cpp
struct Edges {
    float left = 0, top = 0, right = 0, bottom = 0;
};
```

File: `src/ui/layout/edges.h`

## Rendering

### IDrawList
Abstract draw list interface (backed by ImDrawList in practice):
```cpp
class IDrawList {
public:
    virtual void addRect(const Rect& r, uint32_t color, float rounding = 0) = 0;
    virtual void addText(const Pt& pos, uint32_t color, std::string_view text) = 0;
    virtual void addLine(const Pt& a, const Pt& b, uint32_t color, float thickness = 1.0f) = 0;
    // ... more primitives
};
```

File: `src/ui/renderer/idraw_list.h`

### RenderContext
Editor-specific render context with font atlas and texture management:
```cpp
class RenderContext {
    // Font atlas, shader programs, texture cache
};
```

File: `src/editor/visual/render_context.h`

## Math

### Pt (Point)
```cpp
struct Pt {
    float x = 0.0f;
    float y = 0.0f;
};
```

File: `src/ui/math/pt.h`

### Rect
```cpp
struct Rect {
    float x_min = 0.0f, y_min = 0.0f, x_max = 0.0f, y_max = 0.0f;

    [[nodiscard]] float width() const { return x_max - x_min; }
    [[nodiscard]] float height() const { return y_max - y_min; }
    [[nodiscard]] bool contains(const Pt& p) const;
    [[nodiscard]] bool intersects(const Rect& other) const;
};
```

File: `src/ui/math/rect.h`

## Files

| File | Purpose |
|------|---------|
| `src/ui/core/scene.h` | Scene |
| `src/ui/core/widget.h` | Widget |
| `src/ui/core/grid.h` | Grid spatial index |
| `src/ui/core/small_vector.h` | Small vector optimization |
| `src/ui/layout/linear_layout.h` | Linear layout algorithm |
| `src/ui/layout/linear_layout_algo.h` | Layout algorithm internals |
| `src/ui/layout/edges.h` | Edge insets |
| `src/ui/math/pt.h` | Point |
| `src/ui/math/rect.h` | Rectangle |
| `src/ui/renderer/idraw_list.h` | Draw list interface |
| `src/ui/renderer/render_context.h` | Editor render context |
| `src/ui/renderer/tooltip.h` | Tooltip rendering |
