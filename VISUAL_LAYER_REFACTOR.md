# Visual Layer Refactoring Plan

## Objective

Break down three oversized files in `src/editor/visual/` to reach the ~150-line target per file, improve compile-time coupling, and enforce single-responsibility.

| File | Current | Target |
|------|---------|--------|
| `scene/scene.h` | 319 lines, header-only, 1 class, 37 public methods, 10 responsibility areas, 13 consumers | ~80-line header + ~150-line .cpp, 3 extracted helpers |
| `node/widget.h` + `node/widget.cpp` | 207 + 309 = 516 lines, 7 types in header | 3 headers, 2 .cpp files |
| `node/layout.h` + `node/layout.cpp` | 140 + 224 = 364 lines, 7 types | 2 headers, 2 .cpp files |

## Current Dependency Chain

```
data/pt.h
   |
widget.h  (207 lines, 7 types: Bounds, Widget, Header, TypeName, Switch, VerticalToggle, Voltmeter)
  / \
layout.h   port/port.h
(140 lines, 7 types: Edges, Column, Row, Container, Label, Circle, Spacer)
   \      /
   node.h  (95 lines)
     |
visual_node_cache.h
     |
scene.h  (319 lines, header-only, 37 methods)
     |
13 consumers (7 source, 6 test)
```

Every consumer of `scene.h` transitively pulls blueprint, viewport, visual_node_cache, hittest, blueprint_renderer, trigonometry, persist, and spatial_grid.

---

## Phase 1: Scene Decomposition

**Goal:** Move `VisualScene` from header-only to header + .cpp. Extract the three heaviest responsibility areas into dedicated helpers.

### 1a. Create `scene.cpp` (move inline bodies out of header)

Move every method body longer than 3 lines from `scene.h` into a new `scene/scene.cpp`. Keep only trivial one-liner getters inline.

**Before:** 319-line header, 0-line .cpp
**After:** ~80-line header (declarations + trivial inlines), ~180-line .cpp

**Files touched:**
- `scene/scene.h` -- strip method bodies
- `scene/scene.cpp` -- new file, all implementations

### 1b. Extract `SceneMutator` (node/wire CRUD)

Extract the 9 mutation methods into a free-function API or a helper class in `scene/scene_mutator.h/.cpp`:

| Method | Lines | Notes |
|--------|-------|-------|
| `addNode()` | 7 | |
| `removeNode()` | 5 | delegates to removeNodes |
| `addWire()` | 9 | |
| `removeWire()` | 8 | |
| `removeNodes()` | 50 | **largest method**, recursive sub-blueprint cleanup |
| `reconnectWire()` | 13 | |
| `swapWirePortsOnBus()` | 22 | |
| `moveNode()` | 7 | |
| `nextWireId()` | 3 | |

The `removeNodes()` method alone is 50 lines of complex logic (recursive sub-blueprint cleanup, wire pruning, SubBlueprintInstance cleanup). It deserves its own file.

**Files created:**
- `scene/scene_mutator.h` -- free function declarations (~30 lines)
- `scene/scene_mutator.cpp` -- implementations (~130 lines)

`VisualScene` methods become one-line delegates:
```cpp
size_t VisualScene::addNode(Node node) {
    return scene_mutator::add_node(*this, std::move(node));
}
```

### 1c. Move `save()`/`load()` to existing `persist.h/.cpp`

`save()` and `load()` (20 lines combined) already delegate to `save_blueprint_to_file`/`load_blueprint_from_file`. Move them to `persist.cpp` as free functions taking `VisualScene&`. This removes the last persistence coupling from `VisualScene`.

**Files touched:**
- `scene/persist.h` -- add `save_scene()`, `load_scene()` declarations
- `scene/persist.cpp` -- add implementations
- `scene/scene.h` -- remove `save()` and `load()` methods
- Update 2 callers (search for `.save(` and `.load(`)

### Phase 1 Result

| File | Lines |
|------|-------|
| `scene/scene.h` | ~80 (declarations + trivial inlines) |
| `scene/scene.cpp` | ~60 (rendering, hit-test, utility implementations) |
| `scene/scene_mutator.h` | ~30 |
| `scene/scene_mutator.cpp` | ~130 |
| `scene/persist.h` | ~15 (was 10, +2 functions) |
| `scene/persist.cpp` | ~55 (was 34, +20 lines) |

### Phase 1 Tests

- Run full test suite (1414 tests). No new tests needed since this is a pure structural refactor with identical behavior.
- Verify `test_visual_scene.cpp`, `test_persist.cpp`, `test_node_deletion.cpp`, `test_multi_window.cpp`, `test_inspector.cpp`, `test_context_menu.cpp` all still pass.

---

## Phase 2: Widget Extraction

**Goal:** Split `widget.h` (7 types) and `widget.cpp` (5 widget implementations) into focused files.

### 2a. Extract `Bounds` to its own header

`Bounds` is a 7-line value type with a `contains()` method. It is used by `layout.h` and `port.h` independently of any widget.

**Files created:**
- `node/bounds.h` (~15 lines, just the struct)

**Files touched:**
- `widget.h` -- remove `Bounds`, add `#include "bounds.h"`
- `layout.h` -- can now include `bounds.h` instead of `widget.h` if needed
- `port/port.h` -- same

### 2b. Extract `Widget` base class to `widget_base.h`

Move the abstract `Widget` class (lines 26-60) to its own header. This is the interface that `layout.h` depends on.

**Files created:**
- `node/widget_base.h` (~50 lines: forward decl of `IDrawList`, `Widget` class, `Bounds` include)

**Files touched:**
- `widget.h` -- becomes the "all widgets" convenience include OR is replaced entirely
- `layout.h` -- include `widget_base.h` instead of `widget.h`
- `port/port.h` -- include `widget_base.h` if it only needs the base

### 2c. Split concrete widgets into `content_widgets.h/.cpp`

The 5 concrete widgets (`HeaderWidget`, `TypeNameWidget`, `SwitchWidget`, `VerticalToggleWidget`, `VoltmeterWidget`) are only used in:
- `node.cpp` (buildLayout)
- `node_content_renderer.cpp`
- `test_widget.cpp`

Move them to `node/content_widgets.h` (declarations) and `node/content_widgets.cpp` (implementations).

**Before:**
| File | Lines |
|------|-------|
| `widget.h` | 207 |
| `widget.cpp` | 309 |

**After:**
| File | Lines |
|------|-------|
| `node/bounds.h` | ~15 |
| `node/widget_base.h` | ~50 |
| `node/content_widgets.h` | ~120 (5 concrete widget declarations) |
| `node/content_widgets.cpp` | ~300 (implementations, could split further) |

**Optional further split:** If `content_widgets.cpp` is still too large, split `VoltmeterWidget::render()` (69 lines of gauge arc/tick/needle drawing) into `gauge_renderer.h/.cpp`.

### Phase 2 Tests

- All `test_widget.cpp` tests must pass (49 tests across 18 suites).
- All `test_layout.cpp` tests must pass (35 tests).
- Build and run full suite.

---

## Phase 3: Layout Cleanup

**Goal:** Split `layout.h` (7 types) and `layout.cpp` (7 implementations) into leaf vs container types.

### 3a. Extract `Edges` to its own header

`Edges` is a 9-line POD struct used by `Container` and by `node.cpp`. It has no dependencies.

**Files created:**
- `node/edges.h` (~15 lines)

### 3b. Split layout containers from layout primitives

| Category | Types | Where used |
|----------|-------|------------|
| **Containers** | `Column`, `Row`, `Container` | `node.cpp`, `node_content_renderer.cpp`, `test_layout.cpp` |
| **Primitives** | `Label`, `Circle`, `Spacer` | `node.cpp`, `node_content_renderer.cpp`, `test_layout.cpp` |

These share the same consumers, so the split is for readability, not compile-time savings.

**Option A (recommended):** Keep `layout.h`/`layout.cpp` as-is but extract `Edges` and depend on `widget_base.h` instead of `widget.h`. This alone drops the transitive widget dependency from 7 types to 1 abstract base.

**Option B (aggressive):** Create `layout_containers.h/.cpp` (Column, Row, Container) and `layout_primitives.h/.cpp` (Label, Circle, Spacer). Only do this if file sizes are still above target.

**After (Option A):**
| File | Lines |
|------|-------|
| `node/edges.h` | ~15 |
| `node/layout.h` | ~125 (was 140, minus Edges) |
| `node/layout.cpp` | ~210 (was 224, minus Edges) |

This is close enough to the 150-line target for the header. The .cpp at 210 lines is acceptable since it's 7 simple implementations with no complex logic.

### Phase 3 Tests

- All `test_layout.cpp` tests must pass (35 tests).
- Full suite.

---

## Execution Order and Dependencies

```
Phase 1a (scene.cpp)
    |
Phase 1b (scene_mutator)  -- depends on 1a
    |
Phase 1c (persist save/load) -- independent of 1b, depends on 1a
    |
Phase 2a (Bounds extraction) -- independent of Phase 1
    |
Phase 2b (Widget base) -- depends on 2a
    |
Phase 2c (content_widgets split) -- depends on 2b
    |
Phase 3a (Edges extraction) -- independent of Phase 2
    |
Phase 3b (layout include fix) -- depends on 2b and 3a
```

Phases 1 and 2/3 are independent and can be done in any order. Within each phase, the sub-steps must be sequential.

**Recommended order:** 1a -> 1b -> 1c -> 2a -> 2b -> 2c -> 3a -> 3b

Each step should be a single commit with a passing test suite.

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Breaking 13 scene.h consumers | High | Move bodies to .cpp first (1a), then extract. Compile after each step. |
| Include cycle after widget split | Medium | `widget_base.h` has zero project dependencies (only `data/pt.h` and forward-decl `IDrawList`). No cycle possible. |
| Test breakage from moved headers | Low | All tests include by function, not by header path. Update includes mechanically. |
| `scene_mutator` needs friend access to `VisualScene` | Medium | Use public API only. `VisualScene` already exposes `blueprint()`, `cache()`, `invalidateSpatialGrid()` publicly -- mutator needs nothing private. |

---

## Success Criteria

1. No file exceeds ~150 lines (headers) or ~200 lines (.cpp), with the exception of `content_widgets.cpp` (~300) which can be split further if desired.
2. `scene.h` has zero inline method bodies longer than 3 lines.
3. `layout.h` and `widget_base.h` do not transitively include each other's concrete types.
4. All 1414+ tests pass after each phase.
5. Build time for incremental changes improves (fewer files recompiled when a widget changes).
