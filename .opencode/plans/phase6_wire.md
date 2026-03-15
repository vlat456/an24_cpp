# Phase 6: Wire, WireEnd, RoutingPoint -- Implementation Plan (v2)

## Design: Virtual worldMin/worldMax

The correct Qt-style solution to the Wire bounding box problem is to make
`worldMin()` and `worldMax()` **virtual** in the Widget base class. This is
analogous to Qt's virtual `QGraphicsItem::boundingRect()`.

### Why virtual is the right call

- Widget already has a vtable (`virtual ~Widget()`, `virtual isClickable()`, etc.)
  so there is zero additional overhead.
- Different widgets fundamentally have different bounding regions: a rectangular
  node uses `worldPos() + size_`, while a wire's bounds span its polyline.
- Making these virtual is a clean extensibility point that we won't need to touch again.
- The change affects exactly 2 lines in `widget.h` (add `virtual` keyword).
  All existing subclasses inherit the default implementation unchanged.

### Impact analysis

Production code calling `worldMin()`/`worldMax()`:
- `widget.h:30-31` -- definitions (add `virtual`)
- `widget.h:33-37` -- `contains()` calls both internally (gets correct dispatch for free)
- `grid.cpp:8-9` -- `Grid::calcBounds()` calls through `Widget*` (virtual dispatch works)

No existing subclass overrides these methods. All existing tests pass unchanged.

---

## Coordinate Model (clean, no hacks)

With virtual `worldMin()`/`worldMax()`, Wire keeps `local_pos_` at `(0, 0)`:

- **Wire** is a root widget at `local_pos_ = (0, 0)`. Its `worldPos() = (0, 0)`.
- **RoutingPoint** is a child of Wire. It stores absolute world coordinates in
  `local_pos_`. Since Wire's `worldPos() = (0, 0)`, `rp.worldPos() = (0,0) + rp.localPos() = rp.localPos()`. Correct.
- **Wire::worldMin()/worldMax()** compute the bounding box by iterating the polyline
  (start endpoint, routing points, end endpoint) and returning min/max + padding.
- **`polyline()`** uses `child->worldPos()` consistently for everything. No special cases.

This is the Qt-style approach: the parent-child coordinate system works correctly,
and the bounding box is computed via virtual override.

---

## Files to Modify

### `src/editor/visual/widget.h` (2-line change)

```diff
-    Pt worldMin() const { return worldPos(); }
-    Pt worldMax() const { return worldPos() + size_; }
+    virtual Pt worldMin() const { return worldPos(); }
+    virtual Pt worldMax() const { return worldPos() + size_; }
```

---

## Files to Create

### 1. `src/editor/visual/wire/wire_end.h`

```cpp
#pragma once
#include "visual/widget.h"

namespace visual {

class Wire;

/// WireEnd lives as child of Port.
/// Has non-owning back-pointer to the Wire it belongs to.
/// On destruction, notifies Wire so it can self-remove from Scene.
class WireEnd : public Widget {
public:
    explicit WireEnd(Wire* wire) : wire_(wire) {}
    ~WireEnd() override;

    Wire* wire() const { return wire_; }
    void clearWire() { wire_ = nullptr; }

private:
    Wire* wire_;
};

} // namespace visual
```

### 2. `src/editor/visual/wire/wire_end.cpp`

```cpp
#include "wire_end.h"
#include "wire.h"

namespace visual {

WireEnd::~WireEnd() {
    if (wire_) wire_->onEndpointDestroyed(this);
}

} // namespace visual
```

### 3. `src/editor/visual/wire/routing_point.h`

```cpp
#pragma once
#include "visual/widget.h"

namespace visual {

/// RoutingPoint is a child of Wire. Clickable for Grid tracking and drag.
class RoutingPoint : public Widget {
public:
    explicit RoutingPoint(Pt pos) { local_pos_ = pos; }

    bool isClickable() const override { return true; }
};

} // namespace visual
```

### 4. `src/editor/visual/wire/wire.h`

```cpp
#pragma once
#include "visual/widget.h"
#include <vector>
#include <string>

struct IDrawList;

namespace visual {

class WireEnd;
class RoutingPoint;

/// Wire is a root-level widget in the Scene.
/// Connects two WireEnds (which live as children of Ports).
/// Owns RoutingPoint children for polyline bends.
///
/// Bounding box: overrides worldMin()/worldMax() to compute from polyline.
/// Wire's local_pos_ stays at (0,0) — it has no positional meaning.
/// RoutingPoints store absolute world coords in their local_pos_.
class Wire : public Widget {
public:
    Wire(const std::string& id, WireEnd* start, WireEnd* end);
    ~Wire() override;

    std::string_view id() const override { return id_; }
    bool isClickable() const override { return true; }

    WireEnd* start() const { return start_; }
    WireEnd* end() const { return end_; }

    /// Build polyline: start -> routing points -> end (world coords)
    std::vector<Pt> polyline() const;

    /// Override bounding box for Grid spatial indexing.
    Pt worldMin() const override;
    Pt worldMax() const override;

    RoutingPoint* addRoutingPoint(Pt pos, size_t index);
    void removeRoutingPoint(size_t index);

    void render(IDrawList* dl, float zoom) const override;

    /// Called by WireEnd destructor — triggers deferred self-removal
    void onEndpointDestroyed(WireEnd* end);

    static constexpr float WIRE_THICKNESS = 1.5f;
    static constexpr uint32_t WIRE_COLOR = 0xFFCCCCCC;

private:
    std::string id_;
    WireEnd* start_;
    WireEnd* end_;

    static constexpr float BBOX_PADDING = 4.0f;
};

} // namespace visual
```

### 5. `src/editor/visual/wire/wire.cpp`

```cpp
#include "wire.h"
#include "wire_end.h"
#include "routing_point.h"
#include "visual/scene.h"
#include <algorithm>
#include <cfloat>

namespace visual {

Wire::Wire(const std::string& id, WireEnd* start, WireEnd* end)
    : id_(id), start_(start), end_(end) {}

Wire::~Wire() {
    if (start_) start_->clearWire();
    if (end_) end_->clearWire();
}

std::vector<Pt> Wire::polyline() const {
    std::vector<Pt> pts;
    if (start_) pts.push_back(start_->worldPos());
    for (const auto& c : children()) {
        pts.push_back(c->worldPos());
    }
    if (end_) pts.push_back(end_->worldPos());
    return pts;
}

Pt Wire::worldMin() const {
    auto pts = polyline();
    if (pts.empty()) return Pt(0, 0);

    float min_x = pts[0].x, min_y = pts[0].y;
    for (size_t i = 1; i < pts.size(); ++i) {
        min_x = std::min(min_x, pts[i].x);
        min_y = std::min(min_y, pts[i].y);
    }
    return Pt(min_x - BBOX_PADDING, min_y - BBOX_PADDING);
}

Pt Wire::worldMax() const {
    auto pts = polyline();
    if (pts.empty()) return Pt(0, 0);

    float max_x = pts[0].x, max_y = pts[0].y;
    for (size_t i = 1; i < pts.size(); ++i) {
        max_x = std::max(max_x, pts[i].x);
        max_y = std::max(max_y, pts[i].y);
    }
    return Pt(max_x + BBOX_PADDING, max_y + BBOX_PADDING);
}

RoutingPoint* Wire::addRoutingPoint(Pt pos, size_t index) {
    auto rp = std::make_unique<RoutingPoint>(pos);
    auto* ptr = rp.get();

    if (index >= children().size()) {
        addChild(std::move(rp));
    } else {
        // Insert at specific position: remove tail, add new, re-add tail
        std::vector<std::unique_ptr<Widget>> tail;
        while (children().size() > index) {
            tail.push_back(removeChild(children().back().get()));
        }
        addChild(std::move(rp));
        for (auto it = tail.rbegin(); it != tail.rend(); ++it) {
            addChild(std::move(*it));
        }
    }

    // Update Grid entry if in scene (bounds changed)
    if (scene() && isClickable()) {
        scene()->grid().update(this);
    }

    return ptr;
}

void Wire::removeRoutingPoint(size_t index) {
    if (index < children().size()) {
        removeChild(children()[index].get());

        // Update Grid entry if in scene (bounds changed)
        if (scene() && isClickable()) {
            scene()->grid().update(this);
        }
    }
}

void Wire::onEndpointDestroyed(WireEnd* end) {
    if (end == start_) start_ = nullptr;
    if (end == end_) end_ = nullptr;

    if (scene()) scene()->remove(this);
}

void Wire::render(IDrawList* dl, float zoom) const {
    if (!dl) return;
    auto pts = polyline();
    if (pts.size() < 2) return;
    dl->add_polyline(pts.data(), pts.size(), WIRE_COLOR, WIRE_THICKNESS);
}

} // namespace visual
```

### 6. `tests/test_visual_wire.cpp`

```cpp
#include <gtest/gtest.h>
#include "editor/visual/widget.h"
#include "editor/visual/grid.h"
#include "editor/visual/scene.h"
#include "editor/visual/wire/wire.h"
#include "editor/visual/wire/wire_end.h"
#include "editor/visual/wire/routing_point.h"
#include "editor/visual/port/visual_port.h"

// ============================================================
// Helpers
// ============================================================

/// Minimal parent to host a Port (simulates a NodeWidget)
class FakeNode : public visual::Widget {
public:
    FakeNode(const std::string& id, Pt pos) : id_(id) {
        local_pos_ = pos;
        size_ = Pt(100, 60);
    }
    std::string_view id() const override { return id_; }

private:
    std::string id_;
};

// ============================================================
// Construction & Properties
// ============================================================

TEST(WireTest, Construction) {
    visual::WireEnd start(nullptr);
    visual::WireEnd end(nullptr);
    visual::Wire wire("w1", &start, &end);

    EXPECT_EQ(wire.start(), &start);
    EXPECT_EQ(wire.end(), &end);
}

TEST(WireTest, Id) {
    visual::Wire wire("wire_42", nullptr, nullptr);
    EXPECT_EQ(wire.id(), "wire_42");
}

TEST(WireTest, WireIsClickable) {
    visual::Wire wire("w", nullptr, nullptr);
    EXPECT_TRUE(wire.isClickable());
}

TEST(WireTest, WireEndNotClickable) {
    visual::WireEnd we(nullptr);
    EXPECT_FALSE(we.isClickable());
}

TEST(WireTest, RoutingPointIsClickable) {
    visual::RoutingPoint rp(Pt(10, 20));
    EXPECT_TRUE(rp.isClickable());
}

// ============================================================
// Polyline
// ============================================================

TEST(WireTest, PolylineBasic) {
    FakeNode node_a("a", Pt(0, 0));
    FakeNode node_b("b", Pt(200, 0));

    auto port_a = std::make_unique<visual::Port>("out", PortSide::Output, PortType::Voltage);
    port_a->setLocalPos(Pt(100, 30));
    auto* pa = port_a.get();
    node_a.addChild(std::move(port_a));

    auto port_b = std::make_unique<visual::Port>("in", PortSide::Input, PortType::Voltage);
    port_b->setLocalPos(Pt(0, 30));
    auto* pb = port_b.get();
    node_b.addChild(std::move(port_b));

    auto we_start = std::make_unique<visual::WireEnd>(nullptr);
    auto we_end = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_start.get();
    auto* we = we_end.get();
    pa->addChild(std::move(we_start));
    pb->addChild(std::move(we_end));

    visual::Wire wire("w1", ws, we);

    auto pl = wire.polyline();
    ASSERT_EQ(pl.size(), 2u);
    // start world = node_a(0,0) + port(100,30) + wireEnd(0,0)
    EXPECT_FLOAT_EQ(pl[0].x, 100.0f);
    EXPECT_FLOAT_EQ(pl[0].y, 30.0f);
    // end world = node_b(200,0) + port(0,30) + wireEnd(0,0)
    EXPECT_FLOAT_EQ(pl[1].x, 200.0f);
    EXPECT_FLOAT_EQ(pl[1].y, 30.0f);
}

TEST(WireTest, PolylineWithRouting) {
    FakeNode node_a("a", Pt(0, 0));
    FakeNode node_b("b", Pt(200, 0));

    auto pa = std::make_unique<visual::Port>("out", PortSide::Output, PortType::Voltage);
    pa->setLocalPos(Pt(100, 30));
    auto* pa_ptr = pa.get();
    node_a.addChild(std::move(pa));

    auto pb = std::make_unique<visual::Port>("in", PortSide::Input, PortType::Voltage);
    pb->setLocalPos(Pt(0, 30));
    auto* pb_ptr = pb.get();
    node_b.addChild(std::move(pb));

    auto we_s = std::make_unique<visual::WireEnd>(nullptr);
    auto we_e = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_s.get();
    auto* we = we_e.get();
    pa_ptr->addChild(std::move(we_s));
    pb_ptr->addChild(std::move(we_e));

    visual::Wire wire("w1", ws, we);
    wire.addRoutingPoint(Pt(150, 50), 0);

    auto pl = wire.polyline();
    ASSERT_EQ(pl.size(), 3u);
    EXPECT_FLOAT_EQ(pl[0].x, 100.0f);   // start
    EXPECT_FLOAT_EQ(pl[1].x, 150.0f);   // routing point (worldPos = wire(0,0) + rp(150,50))
    EXPECT_FLOAT_EQ(pl[1].y, 50.0f);
    EXPECT_FLOAT_EQ(pl[2].x, 200.0f);   // end
}

TEST(WireTest, PolylineNullStart) {
    FakeNode node_b("b", Pt(200, 0));
    auto pb = std::make_unique<visual::Port>("in", PortSide::Input, PortType::Voltage);
    pb->setLocalPos(Pt(0, 30));
    auto* pb_ptr = pb.get();
    node_b.addChild(std::move(pb));

    auto we_e = std::make_unique<visual::WireEnd>(nullptr);
    auto* we = we_e.get();
    pb_ptr->addChild(std::move(we_e));

    visual::Wire wire("w1", nullptr, we);
    auto pl = wire.polyline();
    ASSERT_EQ(pl.size(), 1u);
    EXPECT_FLOAT_EQ(pl[0].x, 200.0f);
    EXPECT_FLOAT_EQ(pl[0].y, 30.0f);
}

TEST(WireTest, PolylineNullBoth) {
    visual::Wire wire("w1", nullptr, nullptr);
    auto pl = wire.polyline();
    EXPECT_TRUE(pl.empty());
}

// ============================================================
// Bounding Box (virtual worldMin/worldMax override)
// ============================================================

TEST(WireTest, BoundsFromPolyline) {
    FakeNode node_a("a", Pt(100, 100));
    FakeNode node_b("b", Pt(300, 200));

    auto pa = std::make_unique<visual::Port>("out", PortSide::Output, PortType::Voltage);
    pa->setLocalPos(Pt(0, 0));
    auto* pa_ptr = pa.get();
    node_a.addChild(std::move(pa));

    auto pb = std::make_unique<visual::Port>("in", PortSide::Input, PortType::Voltage);
    pb->setLocalPos(Pt(0, 0));
    auto* pb_ptr = pb.get();
    node_b.addChild(std::move(pb));

    auto we_s = std::make_unique<visual::WireEnd>(nullptr);
    auto we_e = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_s.get();
    auto* we = we_e.get();
    pa_ptr->addChild(std::move(we_s));
    pb_ptr->addChild(std::move(we_e));

    visual::Wire wire("w1", ws, we);

    // Polyline: (100,100) -> (300,200)
    // worldMin = (100-4, 100-4) = (96, 96)
    // worldMax = (300+4, 200+4) = (304, 204)
    Pt mn = wire.worldMin();
    Pt mx = wire.worldMax();
    EXPECT_FLOAT_EQ(mn.x, 96.0f);
    EXPECT_FLOAT_EQ(mn.y, 96.0f);
    EXPECT_FLOAT_EQ(mx.x, 304.0f);
    EXPECT_FLOAT_EQ(mx.y, 204.0f);
}

TEST(WireTest, BoundsEmptyPolyline) {
    visual::Wire wire("w", nullptr, nullptr);
    Pt mn = wire.worldMin();
    Pt mx = wire.worldMax();
    EXPECT_FLOAT_EQ(mn.x, 0.0f);
    EXPECT_FLOAT_EQ(mn.y, 0.0f);
    EXPECT_FLOAT_EQ(mx.x, 0.0f);
    EXPECT_FLOAT_EQ(mx.y, 0.0f);
}

TEST(WireTest, BoundsVirtualDispatch) {
    // Verify Grid sees the overridden worldMin/worldMax via Widget*
    FakeNode node_a("a", Pt(100, 100));
    auto pa = std::make_unique<visual::Port>("out", PortSide::Output, PortType::Voltage);
    pa->setLocalPos(Pt(0, 0));
    auto* pa_ptr = pa.get();
    node_a.addChild(std::move(pa));

    auto we_s = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_s.get();
    pa_ptr->addChild(std::move(we_s));

    auto wire = std::make_unique<visual::Wire>("w1", ws, nullptr);
    visual::Widget* w = wire.get(); // base pointer

    // Via base pointer, should still get Wire's override
    Pt mn = w->worldMin();
    EXPECT_FLOAT_EQ(mn.x, 96.0f);
    EXPECT_FLOAT_EQ(mn.y, 96.0f);
}

// ============================================================
// Routing Points
// ============================================================

TEST(WireTest, AddRoutingPoint) {
    visual::Wire wire("w", nullptr, nullptr);
    auto* rp = wire.addRoutingPoint(Pt(50, 60), 0);
    EXPECT_NE(rp, nullptr);
    EXPECT_EQ(wire.children().size(), 1u);
}

TEST(WireTest, AddRoutingPointOrdered) {
    visual::Wire wire("w", nullptr, nullptr);
    wire.addRoutingPoint(Pt(10, 0), 0);
    wire.addRoutingPoint(Pt(30, 0), 1);
    wire.addRoutingPoint(Pt(20, 0), 1); // insert between

    ASSERT_EQ(wire.children().size(), 3u);
    EXPECT_FLOAT_EQ(wire.children()[0]->localPos().x, 10.0f);
    EXPECT_FLOAT_EQ(wire.children()[1]->localPos().x, 20.0f);
    EXPECT_FLOAT_EQ(wire.children()[2]->localPos().x, 30.0f);
}

TEST(WireTest, RemoveRoutingPoint) {
    visual::Wire wire("w", nullptr, nullptr);
    wire.addRoutingPoint(Pt(50, 60), 0);
    EXPECT_EQ(wire.children().size(), 1u);
    wire.removeRoutingPoint(0);
    EXPECT_EQ(wire.children().size(), 0u);
}

// ============================================================
// Cascade Destruction
// ============================================================

TEST(WireTest, CascadeDestructionViaPort) {
    visual::Scene scene;

    // Create a node with a port
    auto node = std::make_unique<FakeNode>("node1", Pt(0, 0));
    auto port = std::make_unique<visual::Port>("out", PortSide::Output, PortType::Voltage);
    auto* port_ptr = port.get();
    node->addChild(std::move(port));
    scene.add(std::move(node));

    // Create wire (initially with null endpoints)
    auto wire = std::make_unique<visual::Wire>("w1", nullptr, nullptr);
    auto* wire_ptr = wire.get();

    // Create WireEnd pointing to the wire, add to port
    auto we = std::make_unique<visual::WireEnd>(wire_ptr);
    port_ptr->addChild(std::move(we));

    scene.add(std::move(wire));
    EXPECT_EQ(scene.roots().size(), 2u); // node + wire

    // Remove the node — destroys Port and WireEnd
    scene.remove(scene.find("node1"));
    scene.flushRemovals(); // node destroyed, WireEnd destructor fires onEndpointDestroyed

    // Wire should be pending removal now (cascaded)
    scene.flushRemovals(); // flush the wire removal
    EXPECT_EQ(scene.roots().size(), 0u);
}

TEST(WireTest, WireDestructorClearsWireEnds) {
    auto we_start = std::make_unique<visual::WireEnd>(nullptr);
    auto we_end = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_start.get();
    auto* we = we_end.get();

    {
        visual::Wire wire("w1", ws, we);
        EXPECT_EQ(ws->wire(), &wire);
        EXPECT_EQ(we->wire(), &wire);
    }
    // Wire destroyed — WireEnds should have wire pointers cleared
    EXPECT_EQ(ws->wire(), nullptr);
    EXPECT_EQ(we->wire(), nullptr);
}

// ============================================================
// Scene Integration
// ============================================================

TEST(WireTest, SceneIntegration) {
    visual::Scene scene;

    FakeNode node_a("a", Pt(100, 100));
    auto pa = std::make_unique<visual::Port>("out", PortSide::Output, PortType::Voltage);
    pa->setLocalPos(Pt(0, 0));
    auto* pa_ptr = pa.get();
    node_a.addChild(std::move(pa));

    auto we_s = std::make_unique<visual::WireEnd>(nullptr);
    auto* ws = we_s.get();
    pa_ptr->addChild(std::move(we_s));

    auto wire = std::make_unique<visual::Wire>("w1", ws, nullptr);
    scene.add(std::move(wire));

    EXPECT_NE(scene.find("w1"), nullptr);

    // Wire should be queryable in Grid near its endpoint
    auto results = scene.grid().queryAs<visual::Wire>(Pt(100, 100), 10.0f);
    EXPECT_GE(results.size(), 1u);
}

TEST(WireTest, RenderNoCrash) {
    visual::Wire wire("w", nullptr, nullptr);
    EXPECT_NO_FATAL_FAILURE(wire.render(nullptr, 1.0f));
}
```

### 7. Modify `tests/CMakeLists.txt` — Add after `visual_node_widget_tests` block

```cmake
# Visual Wire tests (Phase 6 of Scene Graph Refactoring)
add_executable(visual_wire_tests
    test_visual_wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/grid.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire_end.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
)
target_include_directories(visual_wire_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(visual_wire_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(visual_wire_tests)
```

---

## Ownership Model

```
Scene (owns roots)
├── NodeWidget (root)
│   └── Port (child)
│       └── WireEnd (child) ──wire_──→ Wire  (non-owning back-pointer)
│
└── Wire (root, local_pos_ = (0,0))  ──start_/end_──→ WireEnd  (non-owning)
    └── RoutingPoint (child, local_pos_ = absolute world coords)
```

## Destruction Paths

**Path A: Port destroyed first** (node deletion)
1. Node destroyed → Port destroyed → WireEnd destroyed
2. `WireEnd::~WireEnd()` calls `wire_->onEndpointDestroyed(this)`
3. Wire nullifies the endpoint pointer, calls `scene()->remove(this)` (deferred)
4. `flushRemovals()` destroys the Wire
5. `Wire::~Wire()` calls `clearWire()` on remaining WireEnd (if other endpoint still alive)

**Path B: Wire destroyed first** (user deletes wire)
1. Wire destroyed (via `flushRemovals()` or direct)
2. `Wire::~Wire()` calls `start_->clearWire()` and `end_->clearWire()`
3. WireEnds become inert (wire_ = nullptr)
4. WireEnds destroyed later when their Ports are destroyed — no-op in destructor

**Path C: Both endpoints on same node**
1. Node destroyed → both WireEnds destroyed in sequence
2. First `onEndpointDestroyed` pushes Wire to `pending_removals_`
3. Second `onEndpointDestroyed` pushes Wire again
4. `flushRemovals()` erases Wire on first find, second `find_if` returns `end()` → no-op

## Summary of Changes

| File | Action | Lines Changed |
|------|--------|---------------|
| `src/editor/visual/widget.h` | Modify | 2 (add `virtual`) |
| `src/editor/visual/wire/wire_end.h` | Create | ~24 |
| `src/editor/visual/wire/wire_end.cpp` | Create | ~10 |
| `src/editor/visual/wire/routing_point.h` | Create | ~15 |
| `src/editor/visual/wire/wire.h` | Create | ~55 |
| `src/editor/visual/wire/wire.cpp` | Create | ~90 |
| `tests/test_visual_wire.cpp` | Create | ~260 |
| `tests/CMakeLists.txt` | Modify | ~14 (add target) |

**Total: 6 new files, 2 modified files, ~470 lines of new code, 18 tests.**
