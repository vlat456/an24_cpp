# Visual Layer Refactoring Report

**Date**: 2026-03-12  
**Status**: Complete (audited and corrected)  
**Tests**: 1413 passed, 1 skipped, 1 pre-existing failure

## Objective

Break down oversized header-only files in `src/editor/visual/` to improve compile-time coupling and enforce single-responsibility.

## Results

| File | Before | After | Change |
|------|--------|-------|--------|
| `scene/scene.h` | 319 | 134 | -185 |
| `scene/scene.cpp` | 0 | 165 | +165 (new) |
| `node/widget.h` | 207 | 132 | -75 |
| `node/widget_base.h` | - | 34 | +34 (new) |
| `node/bounds.h` | - | 9 | +9 (new) |
| `node/edges.h` | - | 10 | +10 (new) |
| `node/layout.h` | 140 | 128 | -12 |

## Completed Phases

### Phase 1: Scene Decomposition

**1a. Created `scene.cpp`**
- Moved all non-trivial method bodies from header to implementation file
- Methods in `scene.cpp`: `ownsWire`, `addNode`, `addWire`, `removeWire`, `removeNodes`, `reconnectWire`, `swapWirePortsOnBus`, `moveNode`, `reset`, `hitTest`, `hitTestPorts`, `ensureSpatialGrid`, `render`, `detectTooltip`
- Header retains only trivial one-liner getters/delegates and two 3-line grid step methods
- Result: `scene.h` 134 lines, `scene.cpp` 165 lines

**1c. Removed unused persistence methods**
- Deleted `save()` and `load()` methods from `VisualScene`
- These were unused wrappers — actual callers (`Document::save()`/`Document::load()`) call `save_blueprint_to_file`/`load_blueprint_from_file` directly through `persist.h`, bypassing `VisualScene`
- Removed `persist.h` include from `scene.h`
- YAGNI principle applied

### Phase 2: Widget Extraction

**2a. Extracted `Bounds` to `bounds.h`**
- 9-line value type with `contains()` method
- Used independently by `layout.h` and `port.h`

**2b. Extracted `Widget` base class to `widget_base.h`**
- 34-line abstract base class
- `layout.h` now depends only on `widget_base.h`, not full `widget.h`
- `port/port.h` updated to include `widget_base.h` instead of `widget.h`
- Eliminates transitive dependency on 5 concrete widget types from layout and port consumers

### Phase 3: Layout Cleanup

**3a. Extracted `Edges` to `edges.h`**
- 10-line POD struct for margin/padding specification
- Used by `Container` and `node.cpp`

## Audit Fixes (post-refactoring)

The refactoring was audited against the plan (`VISUAL_LAYER_REFACTOR.md`) and five issues were found and corrected:

| # | Issue | Fix |
|---|-------|-----|
| 1 | `[[nodiscard]]` dropped from `addWire` declaration | Restored in `scene.h` |
| 2 | 5 methods with >3-line bodies remained inline in `scene.h` | Moved `hitTest`, `hitTestPorts`, `ensureSpatialGrid`, `render`, `detectTooltip` to `scene.cpp` |
| 3 | `port/port.h` still included `widget.h` instead of `widget_base.h` | Changed to `widget_base.h`; added explicit `#include "visual/node/widget.h"` to `node.cpp` and `test_render.cpp` |
| 4 | `hittest.cpp` missing from 2 test targets | Added to `editor_persist_tests` and `inspector_tests` in CMakeLists.txt |
| 5 | Doc-comments stripped from `scene.h` | Restored `///` class doc, section headers, and key method docs |

## Skipped Work (YAGNI)

| Phase | Reason |
|-------|--------|
| 1b (SceneMutator) | Header already under 150 lines after 1a |
| 2c (content_widgets split) | widget.h already at 132 lines |
| 3b (aggressive layout split) | layout.h at 128 lines, within target |

## Dependency Graph (After)

```
data/pt.h
   |
bounds.h (9 lines)
   |
widget_base.h (34 lines)
   / \
layout.h    widget.h (132 lines, 5 concrete widgets)
(128 lines)     |
   |        node.cpp, node_content_renderer.cpp
port.h ----/
(via widget_base.h)
   |
node.h
   |
visual_node_cache.h
   |
scene.h (134 lines) + scene.cpp (165 lines)
```

Key dependency change: `port.h` now imports `widget_base.h` (1 abstract class), not `widget.h` (6 types). Consumers of `port.h` no longer transitively pull concrete widget implementations.

## Files Created

- `src/editor/visual/scene/scene.cpp`
- `src/editor/visual/node/widget_base.h`
- `src/editor/visual/node/bounds.h`
- `src/editor/visual/node/edges.h`

## Files Modified

- `src/editor/visual/scene/scene.h` — declarations + trivial inlines + doc comments
- `src/editor/visual/node/widget.h` — removed `Bounds`, `Widget` base; includes extracted headers
- `src/editor/visual/node/layout.h` — includes `widget_base.h` and `edges.h` instead of `widget.h`
- `src/editor/visual/port/port.h` — includes `widget_base.h` instead of `widget.h`
- `src/editor/visual/node/node.cpp` — added explicit `#include "visual/node/widget.h"` (lost transitive include)
- `tests/test_render.cpp` — added explicit `#include "visual/node/widget.h"` (lost transitive include)
- `tests/CMakeLists.txt` — added `scene.cpp` to 7 test targets; added `hittest.cpp` to 2 test targets
- `examples/CMakeLists.txt` — added `scene.cpp` to `an24_editor`

## Success Criteria Verification

| Criterion | Status |
|-----------|--------|
| No header exceeds ~150 lines | PASS: scene.h=134, widget.h=132, layout.h=128 |
| No `.cpp` exceeds ~200 lines (except widget.cpp) | PASS: scene.cpp=165 |
| `scene.h` has zero inline bodies >3 lines | PASS: all moved to scene.cpp |
| `layout.h` and `widget_base.h` don't transitively include concrete types | PASS |
| `port.h` doesn't transitively include concrete widgets | PASS |
| `[[nodiscard]]` preserved on `addWire` | PASS |
| All tests pass | PASS: 1413 pass, 1 skip, 1 pre-existing failure |

## Known Pre-existing Issues (not caused by this refactoring)

- `RecentFilesTest.ReAddMovesToFront` — test failure present before refactoring
- `AotComposite.OutputMatchesJitExpansion` — skipped (pre-existing)
- Stale `an24` namespace references in some files — from prior namespace removal work
