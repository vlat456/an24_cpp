# Visual Editor

## Architecture Overview

```
┌───────────────────────────────────────────────────────────────────────┐
│ Document (owns Blueprint + Simulator + WindowManager)                 │
├───────────────────────────────────────────────────────────────────────┤
│ WindowManager → [BlueprintWindow, BlueprintWindow, ...]               │
│                       ↓                                               │
│               Scene (widget tree)                                     │
│               ├── NodeWidget (component nodes)                        │
│               │   ├── Port widgets                                    │
│               │   ├── Content widgets (gauges, switches)              │
│               │   └── Layout containers                               │
│               ├── Wire paths                                          │
│               └── Group containers                                    │
├───────────────────────────────────────────────────────────────────────┤
│ CanvasInput (mouse/keyboard handling)                                 │
│ Viewport (pan/zoom transform)                                         │
├───────────────────────────────────────────────────────────────────────┤
│ RenderContext + IDrawList (ImGui drawing abstraction)                 │
└───────────────────────────────────────────────────────────────────────┘
```

## Key Classes

### Document
Single open document - owns all state:
```cpp
class Document {
    std::string filepath_;
    bp2::Blueprint blueprint_;
    std::unique_ptr<Simulator<DevSolver>> simulator_;
    WindowManager window_manager_;
    bool dirty_ = false;
    
public:
    void step();  // Advance simulation
    void save();
    void save_as(std::string path);
};
```

### WindowManager
MDI window management:
```cpp
class WindowManager {
    std::vector<std::unique_ptr<BlueprintWindow>> windows_;
    BlueprintWindow* active_ = nullptr;
    
public:
    BlueprintWindow* create_window();
    void close_window(BlueprintWindow* w);
    BlueprintWindow* active() const;
};
```

### BlueprintWindow
Single canvas view:
```cpp
class BlueprintWindow {
    Scene scene_;
    Viewport viewport_;
    CanvasInput input_;
    
public:
    void render();
    void handle_input();
};
```

### Scene
Widget tree with Z-order rendering:
```cpp
class Scene {
    std::vector<std::unique_ptr<Widget>> widgets_;
    
public:
    void add_widget(std::unique_ptr<Widget> w);
    void remove_widget(Widget* w);
    Widget* hit_test(Pt screen_pos);
    void render(RenderContext& ctx);
};
```

### Viewport
Pan/zoom transform:
```cpp
class Viewport {
    float pan_x_ = 0, pan_y_ = 0;
    float zoom_ = 1.0f;
    float grid_step_ = 16.0f;
    
public:
    Pt screen_to_world(Pt screen) const;
    Pt world_to_screen(Pt world) const;
    void pan(float dx, float dy);
    void zoom_at(float factor, Pt center);
};
```

## Widget Hierarchy

```cpp
// Base in ui/core/widget.h
class Widget {
public:
    virtual void render(RenderContext& ctx) = 0;
    virtual bool hit_test(Pt pos) = 0;
    virtual Rect bounds() const = 0;
};

// Visual extension in editor/visual/widget.h
class visual::Widget : public ui::Widget {
    RenderLayer layer_;
    Scene* scene_;
public:
    RenderLayer layer() const;
};
```

### NodeWidget
Component node rendering:
```cpp
class NodeWidget : public visual::Widget {
    bp2::Blueprint::Node const* node_;
    std::vector<std::unique_ptr<Port>> ports_;
    Container* content_;
    
public:
    void layout();
    void render(RenderContext& ctx) override;
    Port* hit_test_port(Pt pos);
};
```

### Port
Connection point widget:
```cpp
class Port : public visual::Widget {
    ui::InternedId node_id_;
    ui::InternedId port_name_;
    Domain domain_;
    PortDirection direction_;
    
public:
    Pt connection_point() const;
    Domain domain() const;
};
```

## Render Layers

```cpp
enum class RenderLayer : uint8_t {
    Group  = 0,   // Behind everything
    Text   = 1,   // Behind nodes
    Normal = 2,   // Component nodes
    Wire   = 3,   // Topmost
};
```

Widgets are sorted by layer before rendering.

## Input Handling

### CanvasInput
```cpp
class CanvasInput {
    BlueprintWindow* window_;
    
public:
    void on_mouse_down(Pt pos, int button);
    void on_mouse_up(Pt pos, int button);
    void on_mouse_move(Pt pos);
    void on_scroll(float delta, Pt pos);
    void on_key(int key, bool pressed);
};
```

### Hit Testing
```cpp
Widget* Scene::hit_test(Pt pos) {
    // Reverse Z-order (top first)
    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {
        if ((*it)->hit_test(pos)) return it->get();
    }
    return nullptr;
}
```

## Wire Routing

### Router
Automatic wire pathfinding:
```cpp
class Router {
    Grid grid_;  // Obstacle map
    
public:
    std::vector<Pt> route(Pt start, Pt end);
    void add_obstacle(Rect bounds);
    void remove_obstacle(Rect bounds);
};
```

### Algorithm
A* pathfinding with:
- Manhattan distance heuristic
- Grid-based movement
- Obstacle avoidance
- Wire crossing penalties

## String Interning

O(1) string comparison via 4-byte IDs:

```cpp
class StringInterner {
    std::unordered_map<std::string, ui::InternedId> string_to_id_;
    std::vector<std::string> id_to_string_;
    
public:
    ui::InternedId intern(std::string_view s);
    std::string_view resolve(ui::InternedId id) const;
};

class InternedId {
    uint32_t value_;
public:
    bool operator==(InternedId o) const { return value_ == o.value_; }
    // Trivially copyable, can be used in switch statements
};
```

## Key Files

| Purpose | File |
|---------|------|
| Document | `src/editor/document.h` |
| WindowManager | `src/editor/window_system.h` |
| Scene | `src/editor/visual/scene.h` |
| Viewport | `src/editor/viewport/viewport.h` |
| NodeWidget | `src/editor/visual/node/visual_node.h` |
| Port | `src/editor/visual/port/visual_port.h` |
| Router | `src/editor/visual/wire/` and `src/editor/visual/scene_hittest.h` |
| StringInterner | `src/ui/core/interned_id.h` |
