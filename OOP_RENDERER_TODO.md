# OOP Renderer Re-architecture — TDD Plan

## Motivation

Current `render_blueprint()` is a 400-line monolithic function with 11 parameters.
It mixes wire rendering, node rendering, tooltip detection, simulation queries, and crossing detection.
Cannot support multiple independent windows for different blueprint views (e.g., drill-into subview).

## Target Architecture

```
BlueprintRenderer (owns RenderContext by reference)
├── WireRenderer          — polyline building, crossing gaps, current highlighting
├── NodeRenderer          — delegates to VisualNodeCache → BaseVisualNode::render()
├── TooltipDetector       — port/wire hit testing, simulation value lookup
├── SelectionRenderer     — selection highlights, marquee rectangle
└── GridRenderer          — grid lines (already separate: render_grid)
```

### Core Class: `BlueprintRenderer`

```cpp
class BlueprintRenderer {
public:
    struct Context {
        IDrawList* dl;                      // Drawing backend
        const Viewport& vp;                 // Pan/zoom state
        Pt canvas_min, canvas_max;          // Screen bounds
        VisualNodeCache& cache;             // Visual node cache (reference)
        const Simulator<JIT_Solver>* sim;   // Simulation (nullable)
    };

    explicit BlueprintRenderer(Context ctx);

    /// Render a blueprint view filtered by group_id (nullopt = root level)
    void render(const Blueprint& bp,
                std::optional<std::string> group_id = std::nullopt);

    /// Detect tooltip at hover position
    TooltipInfo detect_tooltip(const Blueprint& bp, Pt world_pos) const;

    /// Selection state (set externally)
    void set_selection(const std::vector<size_t>* nodes, std::optional<size_t> wire);

private:
    Context ctx_;
    WireRenderer wire_renderer_;
    NodeRenderer node_renderer_;
    TooltipDetector tooltip_detector_;
    // selection state
    const std::vector<size_t>* selected_nodes_ = nullptr;
    std::optional<size_t> selected_wire_;
};
```

### Key Principle: `group_id` filtering

`render(bp, group_id)` renders only nodes/wires belonging to that group (or root-level if nullopt).
This enables **multiple independent windows** — each with its own `BlueprintRenderer` + `Context`.

---

## Implementation Plan (TDD: Failing → Green)

### Phase 1: Extract WireRenderer (pure refactor, no behavior change)

**Test first:** Existing wire rendering tests must still pass with extracted class.

- [ ] **1.1** Create `src/editor/renderer/wire_renderer.h` with `WireRenderer` class
  - Method: `render(const Blueprint& bp, IDrawList* dl, const Viewport& vp, Pt canvas_min, ...)`
  - Moves wire polyline building, crossing detection, gap rendering, current highlighting
  - Accepts simulation pointer for energized wire coloring
- [ ] **1.2** Write failing test: `WireRendererTest.RendersPolylineForSingleWire`
  - Use MockDrawList, verify polyline added
- [ ] **1.3** Extract wire rendering code from `render_blueprint()` into `WireRenderer::render()`
- [ ] **1.4** Verify all existing render tests pass (green)

### Phase 2: Extract TooltipDetector (pure refactor)

**Test first:** Existing tooltip tests must pass.

- [ ] **2.1** Create `src/editor/renderer/tooltip_detector.h` with `TooltipDetector` class
  - Method: `detect(const Blueprint& bp, const Viewport& vp, Pt world_pos, Simulator*, VisualNodeCache*) -> TooltipInfo`
  - Moves port hit testing + wire segment distance checking
- [ ] **2.2** Write failing test: `TooltipDetectorTest.DetectsPortHover`
  - Create a node with port, hover near it, expect tooltip
- [ ] **2.3** Extract tooltip detection from `render_blueprint()` into `TooltipDetector::detect()`
- [ ] **2.4** All existing tests pass

### Phase 3: Extract NodeRenderer (pure refactor)

- [ ] **3.1** Create `src/editor/renderer/node_renderer.h` with `NodeRenderer` class
  - Method: `render(const Blueprint& bp, IDrawList* dl, const Viewport& vp, Pt canvas_min, VisualNodeCache*, selection)`
  - Delegates to `VisualNodeCache` → `BaseVisualNode::render()`
- [ ] **3.2** Write failing test: `NodeRendererTest.SkipsHiddenNodes`
- [ ] **3.3** Extract node rendering loop from `render_blueprint()`
- [ ] **3.4** All existing tests pass

### Phase 4: Compose BlueprintRenderer

- [ ] **4.1** Create `src/editor/renderer/blueprint_renderer.h` with `BlueprintRenderer` class
  - Owns WireRenderer, NodeRenderer, TooltipDetector by value
  - `render()` delegates to sub-renderers in order: grid → wires → nodes
  - `detect_tooltip()` delegates to TooltipDetector
- [ ] **4.2** Write failing test: `BlueprintRendererTest.RenderCallsAllSubRenderers`
  - Mock sub-renderers or just verify MockDrawList gets expected primitives
- [ ] **4.3** Implement BlueprintRenderer composing the extracted classes
- [ ] **4.4** Replace `render_blueprint()` free function with `BlueprintRenderer::render()` delegation
- [ ] **4.5** Update `an24_editor.cpp` call site to use `BlueprintRenderer`
- [ ] **4.6** All existing tests pass

### Phase 5: Add group_id filtering for multi-window support

- [ ] **5.1** Write failing test: `BlueprintRendererTest.GroupIdFiltersNodes`
  - Create blueprint with 2 groups, render with group_id, verify only matching nodes rendered
- [ ] **5.2** Add `group_id` parameter to `render()` — filters nodes by `Node::group_id`
- [ ] **5.3** Write failing test: `BlueprintRendererTest.GroupIdFiltersWires`
  - Verify wires between filtered-out nodes are not rendered
- [ ] **5.4** Implement wire filtering by group_id
- [ ] **5.5** All tests pass

### Phase 6: RenderTheme (optional, DRY)

- [ ] **6.1** Extract color constants and `get_node_colors()` into `RenderTheme` struct
- [ ] **6.2** Pass `RenderTheme` via `Context` — enables customizable themes
- [ ] **6.3** Tests pass

---

## File Structure After Refactor

```
src/editor/
├── renderer/
│   ├── blueprint_renderer.h    — main OOP entry point
│   ├── blueprint_renderer.cpp
│   ├── wire_renderer.h
│   ├── wire_renderer.cpp
│   ├── node_renderer.h
│   ├── node_renderer.cpp
│   ├── tooltip_detector.h
│   ├── tooltip_detector.cpp
│   └── render_theme.h          — color constants, node styles
├── render.h                    — kept as thin wrapper / backward compat
├── render.cpp                  — delegates to BlueprintRenderer
├── visual_node.h               — unchanged
└── viewport/viewport.h         — unchanged
```

## Constraints

- **No behavior changes** in Phases 1-4 (pure extracting refactor)
- **MockDrawList** already exists for testing
- **IDrawList** interface stays — BlueprintRenderer uses it via Context
- Each phase should be a single PR-sized unit of work
- All 360+ tests must pass after each phase
