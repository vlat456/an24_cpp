# Scene Graph Refactoring Plan

## Principles

- **Small files**: Each class in its own .h/.cpp pair, ~100-150 lines max
- **Clear folder structure**: Keep current `visual/` organization — it works well
- **Single responsibility**: widget.h = base class only, node/node.h = Node class, etc.
- **Scene is type-agnostic**: Scene operates on `Widget*`, never knows about Node/Wire/Port
- **Namespace `visual::`** for all visual types to avoid collision with `data::Node`, `data::Wire`

Structure:
```
src/editor/visual/
├── widget.h/.cpp          # Base class
├── grid.h/.cpp            # Hit-testing index
├── scene.h/.cpp           # Widget container
├── node/
│   ├── node.h/.cpp        # visual::Node
│   ├── bus_node.h/.cpp
│   ├── ref_node.h/.cpp
│   └── ...
├── port/
│   └── port.h/.cpp        # visual::Port
├── wire/
│   ├── wire.h/.cpp         # visual::Wire
│   ├── wire_end.h/.cpp     # visual::WireEnd
│   └── routing_point.h/.cpp
├── container/
│   ├── container.h/.cpp
│   ├── row.h/.cpp
│   └── column.h/.cpp
├── widget/                 # Leaf widgets
│   ├── label.h/.cpp
│   ├── voltmeter.h/.cpp
│   ├── switch.h/.cpp
│   └── ...
├── hittest.h/.cpp
└── renderer/               # Rendering helpers (unchanged)
```

---

## Goal

Clean scene graph (Qt-style, without signals):
- Single hierarchy: everything is a `Widget` with `scene()` / `parent()`
- World coords via parent chain: `worldPos = parent->worldPos() + localPos`
- Grid tracks clickable `Widget*` directly
- Auto-register on `addChild()`, auto-unregister in `~Widget()`
- `isClickable()` — widgets that participate in hit testing
- `pending_removals_` — deferred delete for safe cascade destruction
- Scene is **type-agnostic** — operates only on Widget*, never concrete types

---

## Widget Tree

```
Scene (owns Grid, owns root widgets, processes pending_removals_)
├── visual::Node (root widget, isClickable)
│     ├── Container (header)
│     │     ├── Label (name)
│     │     └── Label (type)
│     ├── Container (content)
│     │     └── VoltmeterWidget / SwitchWidget / ...
│     └── Container (ports)
│           ├── visual::Port (isClickable)
│           │     ├── Label (port name)
│           │     └── visual::WireEnd (link → Wire, localPos = 0,0)
│           └── visual::Port (isClickable)
│                 └── visual::WireEnd (link → Wire, localPos = 0,0)
├── visual::Node (another node)
│     └── ...
└── visual::Wire (root widget, isClickable)
      ├── RoutingPoint (isClickable, draggable)
      ├── RoutingPoint
      └── ...
```

### Key relationships

- **WireEnd** is child of Port (follows port position automatically)
- **WireEnd** has non-owning pointer to Wire (not parent — just a link)
- **Wire** has non-owning pointers to its two WireEnd's (start_, end_)
- **RoutingPoint** is child of Wire
- **Wire** renders polyline: `start_->worldPos()` → routing points → `end_->worldPos()`

### Cascade destruction

```
scene->remove(node)
  → node added to pending_removals_
  
end of frame: flush pending_removals_
  → node destroyed
    → Port destroyed (child)
      → WireEnd destroyed (child of Port)
        → WireEnd::~WireEnd() calls wire_->onEndpointDestroyed(this)
          → Wire calls scene->remove(this) → added to pending_removals_
    → remaining children destroyed
  → wire destroyed (from pending_removals_)
    → RoutingPoints destroyed (children)
```

---

## Naming

| Old | New |
|-----|-----|
| VisualNode | visual::Node |
| VisualPort | visual::Port |
| VisualWire | visual::Wire |
| VisualScene | visual::Scene |
| isSpatiallyTracked | isClickable |
| SpatialGrid | Grid |
| data Node | data::Node (or keep as-is in `data/node.h`) |

---

## Phase 1: Widget Base Class

File: `src/editor/visual/widget.h`

```cpp
#pragma once
#include "data/pt.h"
#include <string_view>
#include <vector>
#include <memory>

struct IDrawList;

namespace visual {

class Scene;

class Widget {
public:
    virtual ~Widget();
    
    // === Identity ===
    virtual std::string_view id() const { return {}; }
    
    // === Position (local to parent) ===
    Pt localPos() const { return local_pos_; }
    void setLocalPos(Pt p);
    
    // === Size ===
    Pt size() const { return size_; }
    void setSize(Pt s) { size_ = s; }
    
    // === World position (computed via parent chain) ===
    Pt worldPos() const {
        return parent_ ? parent_->worldPos() + local_pos_ : local_pos_;
    }
    Pt worldMin() const { return worldPos(); }
    Pt worldMax() const { return worldPos() + size_; }
    
    bool contains(Pt world_p) const {
        Pt mn = worldMin(), mx = worldMax();
        return world_p.x >= mn.x && world_p.x <= mx.x &&
               world_p.y >= mn.y && world_p.y <= mx.y;
    }
    
    // === Hierarchy ===
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
    
    // === Hit testing: clickable widgets are tracked in Grid ===
    virtual bool isClickable() const { return false; }
    
    // === Layout ===
    virtual Pt preferredSize(IDrawList* dl) const { return size_; }
    virtual void layout(float w, float h) { size_ = Pt(w, h); }
    
    // === Rendering ===
    virtual void render(IDrawList* dl, float zoom) const {}
    void renderTree(IDrawList* dl, float zoom) const;

protected:
    Pt local_pos_{0, 0};
    Pt size_{0, 0};
    
    Scene* scene_ = nullptr;
    Widget* parent_ = nullptr;
    std::vector<std::unique_ptr<Widget>> children_;
    
    friend class Scene;
};

} // namespace visual
```

File: `src/editor/visual/widget.cpp`

```cpp
#include "widget.h"
#include "scene.h"

namespace visual {

Widget::~Widget() {
    if (scene_ && isClickable()) {
        scene_->grid().remove(this);
    }
    // children_ auto-destroyed via unique_ptr destructors
    // which may trigger further cascade removals via pending_removals_
}

void Widget::setLocalPos(Pt p) {
    local_pos_ = p;
    // Update grid for this widget and clickable descendants
    if (scene_ && isClickable()) {
        scene_->grid().update(this);
    }
    for (auto& c : children_) {
        if (c->isClickable()) {
            scene_->grid().update(c.get());
        }
    }
}

void Widget::addChild(std::unique_ptr<Widget> child) {
    child->parent_ = this;
    
    // Propagate scene to entire subtree
    if (scene_) {
        std::function<void(Widget*)> propagate = [&](Widget* w) {
            w->scene_ = scene_;
            if (w->isClickable()) scene_->grid().insert(w);
            for (auto& c : w->children_) propagate(c.get());
        };
        propagate(child.get());
    }
    
    children_.push_back(std::move(child));
}

std::unique_ptr<Widget> Widget::removeChild(Widget* child) {
    auto it = std::find_if(children_.begin(), children_.end(),
        [child](const auto& p) { return p.get() == child; });
    if (it == children_.end()) return nullptr;
    
    // Unregister entire subtree from grid
    if (scene_) {
        std::function<void(Widget*)> unregister = [&](Widget* w) {
            if (w->isClickable()) scene_->grid().remove(w);
            for (auto& c : w->children_) unregister(c.get());
            w->scene_ = nullptr;
        };
        unregister(child);
    }
    
    auto result = std::move(*it);
    children_.erase(it);
    result->parent_ = nullptr;
    return result;
}

void Widget::renderTree(IDrawList* dl, float zoom) const {
    render(dl, zoom);
    for (const auto& c : children_) {
        c->renderTree(dl, zoom);
    }
}

} // namespace visual
```

---

## Phase 2: Grid

File: `src/editor/visual/grid.h`

```cpp
#pragma once
#include "data/pt.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>

namespace visual {

class Widget;

class Grid {
public:
    void clear() { cells_.clear(); bounds_.clear(); }
    
    void insert(Widget* w);
    void remove(Widget* w);
    void update(Widget* w) { remove(w); insert(w); }
    
    std::vector<Widget*> query(Pt world_pos, float margin) const;
    
    template<typename T>
    std::vector<T*> queryAs(Pt world_pos, float margin) const {
        std::vector<T*> result;
        for (auto* w : query(world_pos, margin)) {
            if (auto* t = dynamic_cast<T*>(w)) result.push_back(t);
        }
        return result;
    }

private:
    static constexpr float CELL_SIZE = 64.0f;
    
    struct Cell { std::vector<Widget*> widgets; };
    std::unordered_map<int64_t, Cell> cells_;
    
    // Remember bounds at insert time so remove() works even after widget moved
    struct Bounds { int cx0, cy0, cx1, cy1; };
    std::unordered_map<Widget*, Bounds> bounds_;
    
    static int64_t key(int cx, int cy) { return (int64_t(uint32_t(cx)) << 32) | uint32_t(cy); }
    static int coord(float v) { return int(std::floor(v / CELL_SIZE)); }
    
    Bounds calcBounds(Widget* w) const;
    void insertIntoCells(Widget* w, const Bounds& b);
    void removeFromCells(Widget* w, const Bounds& b);
};

} // namespace visual
```

File: `src/editor/visual/grid.cpp`

```cpp
#include "grid.h"
#include "widget.h"
#include <algorithm>

namespace visual {

Grid::Bounds Grid::calcBounds(Widget* w) const {
    Pt mn = w->worldMin();
    Pt mx = w->worldMax();
    return { coord(mn.x), coord(mn.y), coord(mx.x), coord(mx.y) };
}

void Grid::insert(Widget* w) {
    auto b = calcBounds(w);
    bounds_[w] = b;
    insertIntoCells(w, b);
}

void Grid::remove(Widget* w) {
    auto it = bounds_.find(w);
    if (it == bounds_.end()) return;
    removeFromCells(w, it->second);
    bounds_.erase(it);
}

std::vector<Widget*> Grid::query(Pt pos, float margin) const {
    std::vector<Widget*> result;
    std::unordered_set<Widget*> seen;
    
    int cx0 = coord(pos.x - margin);
    int cy0 = coord(pos.y - margin);
    int cx1 = coord(pos.x + margin);
    int cy1 = coord(pos.y + margin);
    
    for (int cx = cx0; cx <= cx1; cx++) {
        for (int cy = cy0; cy <= cy1; cy++) {
            auto it = cells_.find(key(cx, cy));
            if (it == cells_.end()) continue;
            for (Widget* w : it->second.widgets) {
                if (seen.insert(w).second) result.push_back(w);
            }
        }
    }
    return result;
}

void Grid::insertIntoCells(Widget* w, const Bounds& b) {
    for (int cx = b.cx0; cx <= b.cx1; cx++)
        for (int cy = b.cy0; cy <= b.cy1; cy++)
            cells_[key(cx, cy)].widgets.push_back(w);
}

void Grid::removeFromCells(Widget* w, const Bounds& b) {
    for (int cx = b.cx0; cx <= b.cx1; cx++) {
        for (int cy = b.cy0; cy <= b.cy1; cy++) {
            auto it = cells_.find(key(cx, cy));
            if (it == cells_.end()) continue;
            auto& vec = it->second.widgets;
            vec.erase(std::remove(vec.begin(), vec.end(), w), vec.end());
        }
    }
}

} // namespace visual
```

---

## Phase 3: Scene

File: `src/editor/visual/scene.h`

```cpp
#pragma once
#include "widget.h"
#include "grid.h"
#include <memory>
#include <vector>

namespace visual {

class Scene {
public:
    Scene() = default;
    
    Grid& grid() { return grid_; }
    
    // === Widget lifecycle ===
    Widget* add(std::unique_ptr<Widget> w);
    
    // Deferred removal — safe to call from destructors
    void remove(Widget* w);
    
    // Call once per frame to flush pending_removals_
    void flushRemovals();
    
    // === Query ===
    const std::vector<std::unique_ptr<Widget>>& roots() const { return roots_; }
    
    // Find root widget by id
    Widget* find(std::string_view id) const;
    
    // === Render all ===
    void render(IDrawList* dl, float zoom);

private:
    Grid grid_;
    std::vector<std::unique_ptr<Widget>> roots_;
    std::vector<Widget*> pending_removals_;
    
    void propagateScene(Widget* w);
    void detachScene(Widget* w);
};

} // namespace visual
```

File: `src/editor/visual/scene.cpp`

```cpp
#include "scene.h"
#include <algorithm>

namespace visual {

Widget* Scene::add(std::unique_ptr<Widget> w) {
    auto* ptr = w.get();
    propagateScene(ptr);
    roots_.push_back(std::move(w));
    return ptr;
}

void Scene::remove(Widget* w) {
    pending_removals_.push_back(w);
}

void Scene::flushRemovals() {
    while (!pending_removals_.empty()) {
        // Take snapshot — new removals may be added during destruction
        auto batch = std::move(pending_removals_);
        pending_removals_.clear();
        
        for (Widget* w : batch) {
            // Remove from roots
            auto it = std::find_if(roots_.begin(), roots_.end(),
                [w](const auto& p) { return p.get() == w; });
            if (it != roots_.end()) {
                detachScene(it->get());
                roots_.erase(it);
                // unique_ptr destroys w here
                // → children destroyed → may add to pending_removals_
            }
        }
    }
}

Widget* Scene::find(std::string_view id) const {
    for (const auto& r : roots_) {
        if (r->id() == id) return r.get();
    }
    return nullptr;
}

void Scene::render(IDrawList* dl, float zoom) {
    for (const auto& r : roots_) {
        r->renderTree(dl, zoom);
    }
}

void Scene::propagateScene(Widget* w) {
    w->scene_ = this;
    if (w->isClickable()) grid_.insert(w);
    for (auto& c : w->children()) propagateScene(c.get());
}

void Scene::detachScene(Widget* w) {
    if (w->isClickable()) grid_.remove(w);
    for (auto& c : w->children()) detachScene(c.get());
    w->scene_ = nullptr;
}

} // namespace visual
```

---

## Phase 4: Node

File: `src/editor/visual/node/node.h`

```cpp
#pragma once
#include "visual/widget.h"
#include "data/node.h"

namespace visual {

class Port;
class Container;

class Node : public Widget {
public:
    explicit Node(const ::Node& data);  // ::Node = data::Node from data/node.h
    
    std::string_view id() const override { return id_; }
    bool isClickable() const override { return true; }
    
    const std::string& nodeId() const { return id_; }
    void update(const ::Node& data);
    
    Port* port(const std::string& name) const;
    const std::vector<Port*>& ports() const { return ports_; }
    
    void buildLayout(const ::Node& data);
    void render(IDrawList* dl, float zoom) const override;

private:
    std::string id_;
    std::string name_;
    std::string type_name_;
    
    Container* header_ = nullptr;
    Container* content_ = nullptr;
    Container* ports_left_ = nullptr;
    Container* ports_right_ = nullptr;
    std::vector<Port*> ports_;  // non-owning, children own
};

} // namespace visual
```

---

## Phase 5: Port

File: `src/editor/visual/port/port.h`

```cpp
#pragma once
#include "visual/widget.h"
#include "data/port.h"

namespace visual {

class Port : public Widget {
public:
    Port(std::string name, PortSide side, PortType type);
    
    std::string_view id() const override { return name_; }
    bool isClickable() const override { return true; }
    
    const std::string& name() const { return name_; }
    PortSide side() const { return side_; }
    PortType type() const { return type_; }
    
    void render(IDrawList* dl, float zoom) const override;
    Pt preferredSize(IDrawList* dl) const override;

private:
    std::string name_;
    PortSide side_;
    PortType type_;
};

} // namespace visual
```

---

## Phase 6: Wire, WireEnd, RoutingPoint

File: `src/editor/visual/wire/wire_end.h`

```cpp
#pragma once
#include "visual/widget.h"

namespace visual {

class Wire;

/// WireEnd lives as child of Port.
/// Has non-owning link to the Wire it belongs to.
/// On destruction, notifies Wire → Wire self-removes from Scene.
class WireEnd : public Widget {
public:
    explicit WireEnd(Wire* wire) : wire_(wire) {}
    ~WireEnd() override;
    
    Wire* wire() const { return wire_; }

private:
    Wire* wire_;
};

} // namespace visual
```

File: `src/editor/visual/wire/wire_end.cpp`

```cpp
#include "wire_end.h"
#include "wire.h"

namespace visual {

WireEnd::~WireEnd() {
    if (wire_) wire_->onEndpointDestroyed(this);
}

} // namespace visual
```

File: `src/editor/visual/wire/routing_point.h`

```cpp
#pragma once
#include "visual/widget.h"

namespace visual {

/// RoutingPoint is a child of Wire. Draggable, clickable.
class RoutingPoint : public Widget {
public:
    RoutingPoint(Pt world_pos) { local_pos_ = world_pos; }
    
    bool isClickable() const override { return true; }
};

} // namespace visual
```

File: `src/editor/visual/wire/wire.h`

```cpp
#pragma once
#include "visual/widget.h"
#include <vector>

namespace visual {

class WireEnd;
class RoutingPoint;

class Wire : public Widget {
public:
    Wire(const std::string& id, WireEnd* start, WireEnd* end);
    
    std::string_view id() const override { return id_; }
    bool isClickable() const override { return true; }
    
    WireEnd* start() const { return start_; }
    WireEnd* end() const { return end_; }
    
    // Build polyline for rendering/hit testing
    std::vector<Pt> polyline() const;
    
    // Add/remove routing points (children of Wire)
    RoutingPoint* addRoutingPoint(Pt world_pos, size_t index);
    void removeRoutingPoint(size_t index);
    
    void render(IDrawList* dl, float zoom) const override;
    
    // Called by WireEnd destructor — triggers self-removal
    void onEndpointDestroyed(WireEnd* end);

private:
    std::string id_;
    WireEnd* start_;  // non-owning (child of Port)
    WireEnd* end_;    // non-owning (child of Port)
};

} // namespace visual
```

File: `src/editor/visual/wire/wire.cpp`

```cpp
#include "wire.h"
#include "wire_end.h"
#include "routing_point.h"
#include "visual/scene.h"

namespace visual {

Wire::Wire(const std::string& id, WireEnd* start, WireEnd* end)
    : id_(id), start_(start), end_(end) {}

std::vector<Pt> Wire::polyline() const {
    std::vector<Pt> pts;
    if (start_) pts.push_back(start_->worldPos());
    for (const auto& c : children()) {
        pts.push_back(c->worldPos());
    }
    if (end_) pts.push_back(end_->worldPos());
    return pts;
}

RoutingPoint* Wire::addRoutingPoint(Pt world_pos, size_t index) {
    auto rp = std::make_unique<RoutingPoint>(world_pos);
    auto* ptr = rp.get();
    // Insert at correct position among children
    // (children are ordered: rp0, rp1, ...)
    addChild(std::move(rp));
    return ptr;
}

void Wire::removeRoutingPoint(size_t index) {
    if (index < children().size()) {
        removeChild(children()[index].get());
    }
}

void Wire::onEndpointDestroyed(WireEnd* end) {
    if (end == start_) start_ = nullptr;
    if (end == end_) end_ = nullptr;
    
    // Wire without endpoints must die
    if (scene()) scene()->remove(this);
}

void Wire::render(IDrawList* dl, float zoom) const {
    auto pts = polyline();
    if (pts.size() < 2) return;
    // draw polyline...
}

} // namespace visual
```

---

## Phase 7: Hit Testing

File: `src/editor/visual/hittest.h`

```cpp
#pragma once
#include "visual/widget.h"

namespace visual {

class Scene;
class Node;
class Port;
class Wire;

struct Hit {
    Widget* widget = nullptr;
    Node* node = nullptr;
    Port* port = nullptr;
    Wire* wire = nullptr;
    
    explicit operator bool() const { return widget != nullptr; }
};

Hit hitTest(Scene& scene, Pt pos);
Hit hitTestPorts(Scene& scene, Pt pos);

} // namespace visual
```

File: `src/editor/visual/hittest.cpp`

```cpp
#include "hittest.h"
#include "scene.h"
#include "node/node.h"
#include "port/port.h"
#include "wire/wire.h"
#include "layout_constants.h"
#include "trigonometry.h"

namespace visual {

Hit hitTest(Scene& scene, Pt pos) {
    Hit h;
    
    // Ports first (smallest targets, highest priority)
    for (auto* p : scene.grid().queryAs<Port>(pos, editor_constants::PORT_HIT_RADIUS)) {
        if (editor_math::distance(pos, p->worldPos()) <= editor_constants::PORT_HIT_RADIUS) {
            h.port = p;
            h.node = dynamic_cast<Node*>(p->parent());
            h.widget = p;
            return h;
        }
    }
    
    // Wires
    for (auto* w : scene.grid().queryAs<Wire>(pos, editor_constants::WIRE_SEGMENT_HIT_TOLERANCE)) {
        auto pts = w->polyline();
        // check distance to polyline segments...
        h.wire = w;
        h.widget = w;
        return h;
    }
    
    // Nodes
    for (auto* n : scene.grid().queryAs<Node>(pos, 0)) {
        if (n->contains(pos)) {
            h.node = n;
            h.widget = n;
            return h;
        }
    }
    
    return h;
}

Hit hitTestPorts(Scene& scene, Pt pos) {
    Hit h;
    for (auto* p : scene.grid().queryAs<Port>(pos, editor_constants::PORT_HIT_RADIUS)) {
        if (editor_math::distance(pos, p->worldPos()) <= editor_constants::PORT_HIT_RADIUS) {
            h.port = p;
            h.node = dynamic_cast<Node*>(p->parent());
            h.widget = p;
            return h;
        }
    }
    return h;
}

} // namespace visual
```

---

## Phase 8: Migrate Existing Widgets

Update all leaf widgets to inherit from `visual::Widget`:

- Container (Row, Column, LinearLayout) → visual::Widget subclass
- Label → visual::Widget subclass
- VoltmeterWidget → visual::Widget subclass
- SwitchWidget → visual::Widget subclass
- VerticalToggle → visual::Widget subclass
- HeaderWidget → visual::Widget subclass

All use `worldPos()` from base. None are clickable (isClickable = false).

---

## Phase 9: Delete Old Code

```
DELETE:
  src/editor/visual/spatial/grid.h              → replaced by visual/grid.h
  src/editor/visual/node/widget/widget_base.h   → replaced by visual/widget.h
  src/editor/visual/node/widget/widget_base.cpp  → replaced by visual/widget.cpp
  src/editor/visual/node/visual_node_cache.h    → Scene owns widgets
  src/editor/visual/node/visual_node_cache.cpp
  
REMOVE methods:
  Scene::invalidateSpatialGrid()
  Scene::ensureSpatialGrid()
  Scene::spatial_grid_dirty_
```

---

## Testing

After each phase:
```bash
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest --output-on-failure
```

New tests to write:
- Widget parent/child, worldPos computation
- Grid insert/remove/update/query
- Scene add/remove/flushRemovals
- Cascade: remove Node → Wire auto-removed
- WireEnd lifetime / onEndpointDestroyed

---

## Checklist

- [x] Phase 1: Widget base class (`visual::Widget`)
- [x] Phase 2: Grid (`visual::Grid`)
- [x] Phase 3: Scene (`visual::Scene`)
- [ ] Phase 4: Node (`visual::Node`)
- [ ] Phase 5: Port (`visual::Port`)
- [ ] Phase 6: Wire + WireEnd + RoutingPoint
- [ ] Phase 7: Hit testing
- [x] Phase 8: Migrate leaf widgets (containers, primitives, content widgets — see SCENE_GRAPH_PROGRESS.md)
- [ ] Phase 9: Delete old code
- [ ] All tests pass (47/47 visual tests passing; content widget tests blocked — see SCENE_GRAPH_PROGRESS.md)
