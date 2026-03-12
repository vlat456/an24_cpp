# Visual Layer DRY & SOLID Refactoring

## Audit Findings

### Files Over 150 Lines

| File | Lines | Priority |
|------|-------|----------|
| `node/node.cpp` | 392 | HIGH |
| `node/types/bus_node.cpp` | 174 | MEDIUM |
| `scene/wire_manager.cpp` | 165 | LOW (well-structured) |
| `scene/scene.cpp` | 165 | LOW (well-structured) |
| `canvas_renderer.cpp` | 163 | MEDIUM |
| `spatial/grid.h` | 151 | OK (header-only) |

### DRY Violations

1. **No-op type aliases** - 3 files have `using VisualNodeCache = VisualNodeCache;`
2. **Node render pattern** - 5 node types duplicate: screen transform → fill → content → border → ports
3. **Port label creation** - Identical Container+Label+padding pattern for inputs/outputs
4. **Key handling** - 6 repetitive `if (ImGui::IsKeyPressed(...))` blocks

### SOLID Violations

1. **SRP**: `buildLayout()` handles 3 different layout strategies in one 149-line method
2. **SRP**: `updateNodeContent()` uses `dynamic_cast` chains (code smell)
3. **OCP**: Adding new `NodeContentType` requires changes in 3+ places
4. **ISP**: `VisualNode` has no-op methods that only make sense for subclasses

---

## Refactoring Plan

### Phase 5.1: Quick Fixes (Priority: HIGH) ✅ COMPLETE

- [x] Remove no-op `using VisualNodeCache = VisualNodeCache;` aliases (3 files)
- [x] Clean up any other dead code

### Phase 5.2: Extract Node Render Utility (Priority: HIGH) ✅ COMPLETE

Created `renderer/node_frame.h/.cpp` with reusable node frame rendering:

```cpp
namespace node_frame {
    struct ScreenBounds { Pt min, max; ... };
    
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

**Refactored files:**
- `node/node.cpp`: 392 → 379 lines (-13)
- `node/types/bus_node.cpp`: 174 → 165 lines (-9)
- `node/types/ref_node.cpp`: 60 → 54 lines (-6)

**Benefits:**
- Removes ~28 lines of duplicated code across 3 node render methods
- Single source of truth for node frame rendering
- All 1453 tests pass

### Phase 5.3: Extract Port Label Builder (Priority: MEDIUM)

Create `node/port_label_builder.h/.cpp`:

```cpp
namespace port_layout {
    Widget* build_input_label(Column& col, const EditorPort& port, 
                              std::vector<PortSlot>& slots);
    Widget* build_output_label(Column& col, const EditorPort& port,
                               std::vector<PortSlot>& slots);
    Widget* build_port_row(Row& row, const std::string& left_name, 
                           const std::string& right_name, 
                           PortType left_type, PortType right_type,
                           std::vector<PortSlot>& slots);
}
```

**Benefits:**
- Removes ~60 lines of duplicated layout code from `node.cpp`
- Makes port layout consistent across all node types

### Phase 5.4: Content Widget Strategy (Priority: MEDIUM)

Replace `dynamic_cast` chains with a cleaner update mechanism:

**Option A (Selected): Virtual method on Widget**
- Add `virtual void updateFromContent(const NodeContent& content) {}` to `Widget`
- Each content widget overrides it
- `updateNodeContent()` becomes a single call: `content_widget_->updateFromContent(node_content_)`

**Benefits:**
- Eliminates 3 `dynamic_cast` blocks
- OCP: Adding new content type only requires implementing the virtual method

### Phase 5.5: Extract Key Handler (Priority: LOW)

Create `input/key_handler.h/.cpp`:

```cpp
namespace key_handler {
    struct KeyBinding { ImGuiKey key; InputAction action; };
    
    void process(ImGuiIO& io, CanvasInput& input, 
                 const std::vector<KeyBinding>& bindings,
                 bool read_only);
}
```

**Benefits:**
- Removes repetitive key handling code
- Makes key bindings configurable

---

## Execution Order

1. Phase 5.1 (Quick fixes) → immediate
2. Phase 5.2 (Node frame) → high impact
3. Phase 5.3 (Port labels) → medium impact
4. Phase 5.4 (Content strategy) → medium impact
5. Phase 5.5 (Key handler) → low priority

---

## File Line Count Targets

| File | Current | Target | Strategy |
|------|---------|--------|----------|
| `node/node.cpp` | 392 | ~250 | Extract port_layout + node_frame |
| `node/types/bus_node.cpp` | 174 | ~140 | Use node_frame utility |
| `canvas_renderer.cpp` | 163 | ~120 | Extract key_handler |

---

---

## File Line Count Results (After Phase 5.1-5.2b)

| File | Before | After | Change |
|------|---------|--------|-------|
| `node/node.cpp` | 392 | 379 | -13 |
| `node/types/bus_node.cpp` | 174 | 165 | -9 |
| `node/types/ref_node.cpp` | 60 | 54 | -6 |
| `renderer/node_frame.h` | - | 36 | NEW |
| `renderer/node_frame.cpp` | - | 48 | NEW |

**Total reduction: 28 lines of duplicated rendering code eliminated.

---

## Verification
