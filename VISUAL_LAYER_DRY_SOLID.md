# Visual Layer DRY & SOLID Refactoring

## Summary

**Phases 5.1-7.2 completed** with all 1462 tests passing. **Refactoring complete.**

### Metrics
- **Files modified:** 26
- **Lines removed:** ~250 lines of duplicated code
- **New files:** 6 (`renderer/node_frame.h/.cpp`, `renderer/port_layout_builder.h/.cpp`, `input/key_handler.h`, `containers/linear_layout.h`)
- **Files deleted:** 2 (`containers/row.cpp`, `containers/column.cpp`)
- **Tests:** 1462/1462 passing
- **dynamic_cast eliminated:** 6 instances → 0
- **Mirror duplications eliminated:** Row/Column unified into single `LinearLayout<Axis>`

### Line Count Changes

| File | Before | After | Change |
|------|--------|-------|--------|
| `node/node.cpp` | 392 → 379 → 299 → 286 | 276 | -116 |
| `node/types/bus_node.cpp` | 174 | 165 | -9 |
| `node/types/ref_node.cpp` | 60 | 54 | -6 |
| `node/widget/widget_base.h` | 34 | 37 | +3 |
| `node/widget/widget_base.cpp` | 10 | 14 | +4 |
| `node/widget/content/voltmeter_widget.h` | 36 | 39 | +3 |
| `node/widget/content/voltmeter_widget.cpp` | 78 | 83 | +5 |
| `node/widget/content/switch_widget.h` | 25 | 28 | +3 |
| `node/widget/content/switch_widget.cpp` | 43 | 49 | +6 |
| `node/widget/content/vertical_toggle.h` | 26 | 29 | +3 |
| `node/widget/content/vertical_toggle.cpp` | 61 | 67 | +6 |
| `renderer/node_frame.h` | - | 36 | NEW |
| `renderer/node_frame.cpp` | - | 48 | NEW |
| `renderer/port_layout_builder.h` | - | 37 | NEW |
| `renderer/port_layout_builder.cpp` | - | 101 | NEW |
| `input/key_handler.h` | - | 39 | NEW |
| `canvas_renderer.cpp` | 163 → 143 | 132 | -31 |
| `containers/linear_layout.h` | - | 106 | NEW |
| `containers/row.h` | 20 | 7 | -13 (alias) |
| `containers/column.h` | 20 | 7 | -13 (alias) |
| `containers/row.cpp` | 56 | DELETED | -56 |
| `containers/column.cpp` | 56 | DELETED | -56 |

**Total: ~250 lines of duplicated code eliminated.**

---

## Completed Phases

### Phase 5.1: Quick Fixes ✅

- Removed no-op `using VisualNodeCache = VisualNodeCache;` from 3 files:
  - `renderer/node_renderer.cpp`
  - `renderer/tooltip_detector.cpp`
  - `renderer/blueprint_renderer.cpp`

### Phase 5.2: Node Frame Utility ✅

Created `renderer/node_frame.h/.cpp` with reusable helpers:

```cpp
namespace node_frame {
    struct ScreenBounds { Pt min, max; /* center(), width(), height() */ };
    
    ScreenBounds world_to_screen(Pt world_pos, Pt world_size, 
                                 const Viewport& vp, Pt canvas_min);
    void render_ports(IDrawList& dl, const Viewport& vp, Pt canvas_min,
                      const std::vector<VisualPort>& ports);
    void render_border(IDrawList& dl, const ScreenBounds& bounds,
                       float rounding, bool is_selected);
    void render_fill(IDrawList& dl, const ScreenBounds& bounds,
                     float rounding, uint32_t fill_color);
    uint32_t get_fill_color(const std::optional<NodeColor>& custom_color,
                            uint32_t default_color);
}
```

Refactored 3 node types to use the utility:
- `VisualNode::render()` 
- `BusVisualNode::render()`
- `RefVisualNode::render()`

### Phase 5.3: Port Layout Builder ✅

Created `renderer/port_layout_builder.h/.cpp` to eliminate port label creation boilerplate:

```cpp
namespace port_layout_builder {
    struct PortSlot { Widget* row_container; std::string name; bool is_left; PortType type; float parent_y_offset; };
    struct PortInfo { std::string name; PortType type; };

    Widget* add_input_label_to_column(Column& col, const PortInfo& port, std::vector<PortSlot>& slots);
    Widget* add_output_label_to_column(Column& col, const PortInfo& port, std::vector<PortSlot>& slots);
    Widget* build_port_row(Column& target, const PortInfo* left, const PortInfo* right, std::vector<PortSlot>& slots);
    void build_input_column(Column& col, const std::vector<PortInfo>& inputs, std::vector<PortSlot>& slots);
    void build_output_column(Column& col, const std::vector<PortInfo>& outputs, std::vector<PortSlot>& slots);
}
```

Refactored `VisualNode::buildLayout()`:
- Extracted ~60 lines of repetitive port label/row creation
- Moved `PortSlot` struct to `port_layout_builder` namespace
- `node.cpp` reduced from 379 → 299 lines

### Phase 5.4: Content Widget Strategy ✅

Added virtual `updateFromContent()` method to Widget base class:

```cpp
class Widget {
public:
    virtual void updateFromContent(const NodeContent& content);
};
```

Overrode in content-aware widgets:
- `VoltmeterWidget::updateFromContent()` - updates value
- `SwitchWidget::updateFromContent()` - updates state/tripped
- `VerticalToggleWidget::updateFromContent()` - updates state/tripped

Refactored `VisualNode::updateNodeContent()`:
- Eliminated 5 `dynamic_cast` calls (down from 6 to 1)
- Replaced type-specific branches with polymorphic dispatch
- `node.cpp` reduced from 299 → 286 lines

### Phase 5.5: Key Handler ✅

Created `input/key_handler.h` (header-only, 39 lines) with table-driven key mapping:

```cpp
namespace key_handler {
    struct KeyMapping { ImGuiKey imgui_key; Key app_key; bool needs_write; };
    
    inline constexpr KeyMapping EDITOR_KEYS[] = { ... };
    
    template <typename Callback>
    void process_keys(bool want_capture, bool read_only, Callback&& cb);
}
```

Refactored `canvas_renderer.cpp`:
- Replaced 6 repetitive `if (ImGui::IsKeyPressed(...))` blocks with single `process_keys` call
- `canvas_renderer.cpp` reduced from 163 → 143 lines (-20 lines)

---

### Phase 6.1: Bug Regression Suite ✅

Added 9 new targeted regression tests in `test_refactoring_regression.cpp`:
- `VoltmeterWidget` division-by-zero (degenerate ranges)
- `RefVisualNode` null-dereference protection for empty port vectors
- `SpatialGrid` invalidation after `moveNode`
- `VisualNode` creation order/address stability in `addNode`
- `Container` layout clamping for negative dimensions

### Phase 6.2: Elimination of `dynamic_cast` in `node.cpp` ✅

- Introduced `inner_content_widget_` and `center_column_` member pointers in `VisualNode`
- Replaced the `dynamic_cast<Container*>` chains in `updateNodeContent()` and `getContentBounds()` with direct pointer access
- Removed the `dynamic_cast<Row*>` in the constructor by storing the pointer during the layout build phase
- **Result: 0 `dynamic_cast` remaining in `node.cpp`**

### Phase 6.3: DRY Port Building ✅

- Unified input/output port resolution loops in `node.cpp` into a single `add_ports` lambda
- Reduced code duplication by ~15 lines
- `node.cpp` reduced from 286 → 276 lines

### Phase 6.4: Unify Row/Column via LinearLayout ✅

Created `containers/linear_layout.h` - a template class parameterized on `Axis`:

```cpp
enum class Axis { Horizontal, Vertical };

template <Axis axis>
class LinearLayout : public Widget {
    // All layout logic written once, axis resolved at compile time via if constexpr
    static float main(Pt p);   // x for Horizontal, y for Vertical
    static float cross(Pt p);  // y for Horizontal, x for Vertical
    // ...
};
```

Redefined `Row` and `Column` as thin type aliases:
```cpp
using Row = LinearLayout<Axis::Horizontal>;    // row.h
using Column = LinearLayout<Axis::Vertical>;   // column.h
```

- **Deleted** `row.cpp` (56 lines) and `column.cpp` (56 lines) - mirror duplicates eliminated
- Zero runtime overhead (compile-time axis dispatch)
- All 1462 existing tests pass unchanged (backward-compatible aliases)

### Phase 6.5: DRY ImGuiDrawList Adapter ✅

Extracted `make_dl()` helper in `canvas_renderer.cpp` to replace 5 instances of:
```cpp
ImGuiDrawList dl;
dl.dl = draw_list;
```
With:
```cpp
auto dl = make_dl(draw_list);
```

---

### Phase 7.0: Shutdown Crash Fix ✅

**Bug**: `EditorApp::shutdown()` was called twice — once explicitly at the end of `run()` and again from the destructor `~EditorApp()`. The second call tried to shut down ImGui backends that were already destroyed, triggering `SIGABRT` (`bd != nullptr && "No renderer backend to shutdown, or already shutdown?"`).

**Fix** (`editor_app.h` + `editor_app.cpp`):
- Added `shutdown_done_` boolean guard to make `shutdown()` idempotent
- Added `ImGui::GetCurrentContext()` null check to protect against partial initialization paths
- Added null checks for `gl_context_` and `window_` before SDL teardown

### Phase 7.1: Wire Endpoint Resolution DRY ✅

**Problem**: The pattern "look up both endpoint nodes, null-check, group_id filter, call `get_port_position` for each end" was duplicated in 4 separate files:
- `renderer/wire/polyline_builder.cpp`
- `spatial/grid.h`
- `hittest/nodes.cpp`
- `renderer/tooltip_detector.cpp`

**Fix**: Added `editor_math::resolve_wire_endpoints()` to `trigonometry.h`:
```cpp
inline std::optional<std::pair<Pt, Pt>> resolve_wire_endpoints(
    const Wire& wire, const Blueprint& bp,
    const std::string& group_id, VisualNodeCache& cache);
```

Refactored all 4 call sites to use the new function, eliminating ~30 lines of boilerplate. Also removed the hand-rolled `node_map` from `polyline_builder.cpp` (now uses `bp.find_node()` via the utility).

### Phase 7.2: Input Dispatch DRY ✅

**Problem**: `canvas_renderer.cpp::handleInput()` repeated the 2-line pattern 7 times:
```cpp
auto action = doc.applyInputResult(win.input.<method>(...), win.group_id);
ws.handleInputAction(action, doc);
```

**Fix**: Extracted a local `dispatch` lambda that captures `doc`, `win.group_id`, and `ws`, reducing each call site to a single line. `canvas_renderer.cpp` reduced from 141 → 132 lines.

---

## Verification

```bash
cmake --build build -j$(sysctl -n hw.ncpu)
ctest  # 1462/1462 tests pass
```

## Remaining Opportunities (Reviewed)

All three candidates were reviewed and determined to be adequate as-is:

- **WireManager consolidation**: Already DRY via `scene_.portPosition()` → `editor_math::get_port_position()`. Adding singular `resolve_wire_endpoint` would duplicate without benefit.
- **Node type factory**: For 4 node types, registry pattern is overkill. Current if-chain is readable and compact.
- **File size audit**: Files at 150-170 lines (`scene.cpp`, `wire_manager.cpp`, `bus_node.cpp`) contain cohesive core logic. `node.cpp` at 276 lines is acceptable for the central visual node implementation.

---

## Conclusion

The Visual Layer refactoring is complete. Key achievements:
- **~250 lines of duplicated code eliminated**
- **6 `dynamic_cast` → 0** via polymorphic dispatch
- **Row/Column unified** into `LinearLayout<Axis>` template
- **Wire endpoint resolution** centralized in `editor_math::resolve_wire_endpoints()`
- **All 1462 tests passing**
