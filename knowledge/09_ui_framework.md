# UI Framework

## Overview

The `src/ui/` directory contains a generic UI framework used by the editor. It provides:
- Widget abstraction
- Layout algorithms
- Drawing primitives
- String interning

## Directory Structure

```
src/ui/
├── core/
│   ├── widget.h         # Base widget class
│   ├── interned_id.h    # String interning
│   └── scene.h          # Widget tree
├── layout/
│   ├── linear_layout.h  # Linear layout
│   ├── linear_layout_algo.h
│   └── edges.h          # Margin/padding
├── math/
│   └── pt.h             # Point/Rect math
└── renderer/
    └── draw_list.h      # Drawing interface
```

## Core Classes

### Widget (Base)
```cpp
namespace ui {

class Widget {
public:
    virtual ~Widget() = default;
    
    virtual void render(RenderContext& ctx) = 0;
    virtual bool hit_test(Pt pos) = 0;
    virtual Rect bounds() const = 0;
    
    Widget* parent() const;
    void set_parent(Widget* p);
    
private:
    Widget* parent_ = nullptr;
};

}
```

### InternedId
O(1) string comparison:
```cpp
class InternedId {
    uint32_t value_;
public:
    InternedId() : value_(0) {}
    explicit InternedId(uint32_t v) : value_(v) {}
    
    uint32_t value() const { return value_; }
    bool valid() const { return value_ != 0; }
    
    bool operator==(InternedId o) const { return value_ == o.value_; }
    bool operator!=(InternedId o) const { return value_ != o.value_; }
    bool operator<(InternedId o) const { return value_ < o.value_; }
};
```

### StringInterner
```cpp
class StringInterner {
    std::unordered_map<std::string, InternedId> string_to_id_;
    std::vector<std::string> id_to_string_;
    
public:
    InternedId intern(std::string_view s);
    std::string_view resolve(InternedId id) const;
    bool contains(std::string_view s) const;
};
```

## Math Types

### Pt (Point)
```cpp
struct Pt {
    float x, y;
    
    Pt operator+(Pt o) const { return {x + o.x, y + o.y}; }
    Pt operator-(Pt o) const { return {x - o.x, y - o.y}; }
    Pt operator*(float s) const { return {x * s, y * s}; }
    
    static Pt zero() { return {0, 0}; }
};
```

### Rect
```cpp
struct Rect {
    float x, y, w, h;
    
    bool contains(Pt p) const {
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }
    
    Pt center() const { return {x + w/2, y + h/2}; }
    Pt top_left() const { return {x, y}; }
    Pt bottom_right() const { return {x + w, y + h}; }
    
    static Rect from_corners(Pt tl, Pt br) {
        return {tl.x, tl.y, br.x - tl.x, br.y - tl.y};
    }
};
```

### Grid
```cpp
struct Grid {
    int col, row;
    
    Grid operator+(Grid o) const { return {col + o.col, row + o.row}; }
    bool operator==(Grid o) const { return col == o.col && row == o.row; }
};
```

## Layout

### Edges (Margin/Padding)
```cpp
struct Edges {
    float left = 0, top = 0, right = 0, bottom = 0;
    
    static Edges all(float v) { return {v, v, v, v}; }
    static Edges horizontal(float v) { return {v, 0, v, 0}; }
    static Edges vertical(float v) { return {0, v, 0, v}; }
};
```

### LinearLayout
```cpp
class LinearLayout {
    Orientation orientation_ = Orientation::Vertical;
    float spacing_ = 0;
    Edges padding_;
    
public:
    void layout(std::vector<Widget*> children, Rect bounds);
    Pt measure(std::vector<Widget*> children);
};

enum class Orientation { Vertical, Horizontal };
```

### LinearLayoutAlgo
```cpp
void linear_layout_algo(
    std::span<Widget*> children,
    Rect bounds,
    Orientation orient,
    float spacing,
    Edges padding
);
```

## Rendering

### IDrawList
Abstraction over ImGui draw list:
```cpp
class IDrawList {
public:
    virtual void add_rect(Rect r, Color color, float rounding = 0) = 0;
    virtual void add_rect_filled(Rect r, Color color, float rounding = 0) = 0;
    virtual void add_line(Pt a, Pt b, Color color, float thickness = 1) = 0;
    virtual void add_text(Pt pos, const char* text, Color color) = 0;
    virtual void add_circle(Pt center, float radius, Color color) = 0;
    virtual void add_bezier(Pt a, Pt cp1, Pt cp2, Pt b, Color color, float thickness) = 0;
};
```

### RenderContext
```cpp
class RenderContext {
    IDrawList* draw_list_;
    Viewport* viewport_;
    
public:
    IDrawList& draw() { return *draw_list_; }
    Viewport& viewport() { return *viewport_; }
    
    Pt world_to_screen(Pt world) const;
    Pt screen_to_world(Pt screen) const;
};
```

## Color

```cpp
struct Color {
    float r, g, b, a;
    
    static Color white() { return {1, 1, 1, 1}; }
    static Color black() { return {0, 0, 0, 1}; }
    static Color red() { return {1, 0, 0, 1}; }
    static Color green() { return {0, 1, 0, 1}; }
    static Color blue() { return {0, 0, 1, 1}; }
    
    Color with_alpha(float a) const { return {r, g, b, a}; }
    uint32_t to_u32() const;  // RGBA packed
};
```

## Usage in Editor

The editor extends `ui::Widget` to create visual elements:

```cpp
// In editor/visual/widget.h
namespace visual {

class Widget : public ui::Widget {
    RenderLayer layer_ = RenderLayer::Normal;
    Scene* scene_ = nullptr;
    
public:
    RenderLayer layer() const { return layer_; }
    Scene* scene() const { return scene_; }
    void set_layer(RenderLayer l) { layer_ = l; }
};

}
```

## Key Files

| Purpose | File |
|---------|------|
| Base Widget | `src/ui/core/widget.h` |
| InternedId | `src/ui/core/interned_id.h` |
| Pt/Rect | `src/ui/math/pt.h` |
| LinearLayout | `src/ui/layout/linear_layout.h` |
| Edges | `src/ui/layout/edges.h` |
| IDrawList | `src/ui/renderer/draw_list.h` |
