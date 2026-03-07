# Task: Move visual/\* files into directory hierarchy + integrate save/load into VisualScene

## Goal

Reorganize the flat `src/editor/` layout into a proper `src/editor/visual/` hierarchy and make VisualScene a real class (not a facade) with save/load.

## Current layout (files to move)

```
src/editor/visual_scene.h       → src/editor/visual/scene/scene.h
src/editor/visual_node.h        → src/editor/visual/node/node.h
src/editor/visual_node.cpp      → src/editor/visual/node/node.cpp
src/editor/visual_port.h        → src/editor/visual/port/port.h
src/editor/visual_port.cpp      → src/editor/visual/port/port.cpp
src/editor/render.h             → src/editor/visual/render.h
src/editor/render.cpp           → src/editor/visual/render.cpp
src/editor/hittest.h            → src/editor/visual/hittest.h
src/editor/hittest.cpp          → src/editor/visual/hittest.cpp
src/editor/trigonometry.h       → src/editor/visual/trigonometry.h
src/editor/widget.h             → src/editor/visual/node/widget.h
src/editor/widget.cpp           → src/editor/visual/node/widget.cpp
src/editor/interfaces.h         → src/editor/visual/interfaces.h
src/editor/persist.h            → src/editor/visual/scene/persist.h
src/editor/persist.cpp          → src/editor/visual/scene/persist.cpp
```

Files that stay in place:

```
src/editor/app.h, app.cpp                    (EditorApp controller)
src/editor/data/*                             (Blueprint, Node, Wire, Pt, Port)
src/editor/viewport/*                         (Viewport)
src/editor/interact/*                         (Interaction)
src/editor/router/*                           (A* router)
src/editor/simulation.h, simulation.cpp       (Simulation bridge)
src/editor/wires/hittest.h, hittest.cpp       (routing point hit test)
src/editor/gl_setup.h                         (OpenGL setup)
```

## Directories already created

```
src/editor/visual/scene/
src/editor/visual/node/
src/editor/visual/port/
```

## Step-by-step instructions

### Step 1: Move files with git mv

Run these commands exactly:

```bash
cd /Users/vladimir/an24_cpp

# Scene
git mv src/editor/visual_scene.h src/editor/visual/scene/scene.h
git mv src/editor/persist.h src/editor/visual/scene/persist.h
git mv src/editor/persist.cpp src/editor/visual/scene/persist.cpp

# Node
git mv src/editor/visual_node.h src/editor/visual/node/node.h
git mv src/editor/visual_node.cpp src/editor/visual/node/node.cpp
git mv src/editor/widget.h src/editor/visual/node/widget.h
git mv src/editor/widget.cpp src/editor/visual/node/widget.cpp

# Port
git mv src/editor/visual_port.h src/editor/visual/port/port.h
git mv src/editor/visual_port.cpp src/editor/visual/port/port.cpp

# Visual root
git mv src/editor/render.h src/editor/visual/render.h
git mv src/editor/render.cpp src/editor/visual/render.cpp
git mv src/editor/hittest.h src/editor/visual/hittest.h
git mv src/editor/hittest.cpp src/editor/visual/hittest.cpp
git mv src/editor/trigonometry.h src/editor/visual/trigonometry.h
git mv src/editor/interfaces.h src/editor/visual/interfaces.h
```

### Step 2: Fix includes in moved files

Every moved file's `#include` paths must be adjusted since the file's location changed. The `src/editor/` directory is in the include path, so all includes are relative to `src/editor/`.

**Key rule**: From `src/editor/visual/scene/scene.h`, to include `src/editor/data/blueprint.h`, use `#include "data/blueprint.h"` (unchanged — since include root is `src/editor/`). To include `src/editor/visual/node/node.h`, use `#include "visual/node/node.h"`.

Here's the full include rewrite map for MOVED files. Only rewrite includes of other moved files:

| Old include        | New include                |
| ------------------ | -------------------------- |
| `"visual_node.h"`  | `"visual/node/node.h"`     |
| `"visual_port.h"`  | `"visual/port/port.h"`     |
| `"visual_scene.h"` | `"visual/scene/scene.h"`   |
| `"render.h"`       | `"visual/render.h"`        |
| `"hittest.h"`      | `"visual/hittest.h"`       |
| `"trigonometry.h"` | `"visual/trigonometry.h"`  |
| `"widget.h"`       | `"visual/node/widget.h"`   |
| `"interfaces.h"`   | `"visual/interfaces.h"`    |
| `"persist.h"`      | `"visual/scene/persist.h"` |

Includes that do NOT change (they refer to non-moved files):

- `"data/blueprint.h"`, `"data/node.h"`, `"data/wire.h"`, `"data/pt.h"`, `"data/port.h"` — stay same
- `"viewport/viewport.h"` — stays same
- `"../jit_solver/simulator.h"` — since `src/` is an include root, change to `"jit_solver/simulator.h"` (cleaner).
- `"../json_parser/json_parser.h"` — since `src/` is an include root, change to `"json_parser/json_parser.h"` (cleaner). Appears in visual_node.h and widget.h.
- `"router/router.h"` — stays same (only used in app.cpp which doesn't move)

#### Files to fix (moved files referencing other moved files):

**src/editor/visual/scene/scene.h** (was visual_scene.h):

```
"visual_node.h"    → "visual/node/node.h"
"hittest.h"        → "visual/hittest.h"
"render.h"         → "visual/render.h"
"trigonometry.h"   → "visual/trigonometry.h"
```

**src/editor/visual/node/node.h** (was visual_node.h):

```
"interfaces.h"     → "visual/interfaces.h"
"visual_port.h"    → "visual/port/port.h"
"widget.h"         → "visual/node/widget.h"
"../json_parser/json_parser.h" → "json_parser/json_parser.h"
```

**src/editor/visual/node/node.cpp** (was visual_node.cpp):

```
"visual_node.h"    → "visual/node/node.h"
"render.h"         → "visual/render.h"
```

**src/editor/visual/port/port.h** (was visual_port.h):

```
"widget.h"         → "visual/node/widget.h"
```

**src/editor/visual/port/port.cpp** (was visual_port.cpp):

```
"visual_port.h"    → "visual/port/port.h"
"render.h"         → "visual/render.h"
```

**src/editor/visual/render.h** (was render.h):

```
"../jit_solver/simulator.h" → "jit_solver/simulator.h"
```

**src/editor/visual/render.cpp** (was render.cpp):

```
"render.h"         → "visual/render.h"
"trigonometry.h"   → "visual/trigonometry.h"
"visual_node.h"    → "visual/node/node.h"
```

**src/editor/visual/hittest.h** (was hittest.h):
No changes needed (only includes data/_ and viewport/_ which stay).

**src/editor/visual/hittest.cpp** (was hittest.cpp):

```
"hittest.h"        → "visual/hittest.h"
"trigonometry.h"   → "visual/trigonometry.h"
"visual_node.h"    → "visual/node/node.h"
```

**src/editor/visual/trigonometry.h** (was trigonometry.h):

```
"visual_node.h"    → "visual/node/node.h"
```

**src/editor/visual/node/widget.h** (was widget.h):

```
"../json_parser/json_parser.h" → "json_parser/json_parser.h"
```

**src/editor/visual/node/widget.cpp** (was widget.cpp):

```
"widget.h"         → "visual/node/widget.h"
"render.h"         → "visual/render.h"
```

**src/editor/visual/scene/persist.h** (was persist.h):
No changes needed (only includes data/blueprint.h).

**src/editor/visual/scene/persist.cpp** (was persist.cpp):

```
"persist.h"        → "visual/scene/persist.h"
```

**src/editor/visual/interfaces.h** (was interfaces.h):
No changes needed (only includes data/pt.h).

### Step 3: Fix includes in NON-moved files that reference moved files

**src/editor/app.h**:

```
"hittest.h"        → "visual/hittest.h"
"visual_scene.h"   → "visual/scene/scene.h"
```

**src/editor/app.cpp**:

```
"hittest.h"        → "visual/hittest.h"
"trigonometry.h"   → "visual/trigonometry.h"
"visual_node.h"    → "visual/node/node.h"
```

(`"wires/hittest.h"` and `"router/router.h"` — stay same, those files didn't move)

**src/editor/wires/hittest.cpp**:

```
"trigonometry.h"   → "visual/trigonometry.h"
```

(`"wires/hittest.h"` stays same)

**src/editor/simulation.cpp**:

```
"persist.h"        → "visual/scene/persist.h"
```

**examples/an24_editor.cpp**:

```
"editor/render.h"  → "editor/visual/render.h"
"editor/persist.h" → "editor/visual/scene/persist.h"
```

**tests/test_hittest.cpp**:

```
"editor/hittest.h"    → "editor/visual/hittest.h"
"editor/trigonometry.h" → "editor/visual/trigonometry.h"
"editor/visual_node.h" → "editor/visual/node/node.h"
```

**tests/test_render.cpp**:

```
"editor/render.h"      → "editor/visual/render.h"
"editor/visual_node.h" → "editor/visual/node/node.h"
```

**tests/test_persist.cpp**:

```
"editor/persist.h"     → "editor/visual/scene/persist.h"
"editor/visual_node.h" → "editor/visual/node/node.h"
```

**tests/test_visual_port.cpp**:

```
"editor/visual_port.h" → "editor/visual/port/port.h"
"editor/render.h"      → "editor/visual/render.h"
```

**tests/test_visual_scene.cpp**:

```
"editor/visual_scene.h" → "editor/visual/scene/scene.h"
```

**tests/test_events.cpp**:

```
"editor/trigonometry.h" → "editor/visual/trigonometry.h"
```

**tests/test_editor_hierarchical.cpp**:

```
"editor/render.h"      → "editor/visual/render.h"
"editor/persist.h"     → "editor/visual/scene/persist.h"
"editor/visual_node.h" → "editor/visual/node/node.h"
```

**tests/test_widget.cpp**:

```
"editor/interfaces.h"  → "editor/visual/interfaces.h"
"editor/widget.h"      → "editor/visual/node/widget.h"
"editor/visual_node.h" → "editor/visual/node/node.h"
"editor/render.h"      → "editor/visual/render.h"
```

**tests/test_router.cpp**:

```
"editor/trigonometry.h" → "editor/visual/trigonometry.h"
"editor/visual_node.h"  → "editor/visual/node/node.h"
```

**tests/test_routing.cpp**:

```
(uses "editor/wires/hittest.h" — stays same, that file didn't move)
```

**tests/test_logical_solver.cpp**:

```
"editor/persist.h"     → "editor/visual/scene/persist.h"
```

**tests/test_simulation.cpp**:

```
"editor/persist.h"     → "editor/visual/scene/persist.h"
```

### Step 4: Fix CMakeLists.txt source file paths

In **tests/CMakeLists.txt**, apply these path rewrites globally:

```
${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp   → ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual_port.cpp   → ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
${CMAKE_SOURCE_DIR}/src/editor/widget.cpp         → ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
${CMAKE_SOURCE_DIR}/src/editor/render.cpp         → ${CMAKE_SOURCE_DIR}/src/editor/visual/render.cpp
${CMAKE_SOURCE_DIR}/src/editor/hittest.cpp        → ${CMAKE_SOURCE_DIR}/src/editor/visual/hittest.cpp
${CMAKE_SOURCE_DIR}/src/editor/persist.cpp        → ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
```

In **examples/CMakeLists.txt**, apply the same path rewrites plus:

```
${CMAKE_SOURCE_DIR}/src/editor/wires/hittest.cpp  → stays same (not moved)
```

### Step 5: Add save() and load() to VisualScene

In `src/editor/visual/scene/scene.h`, add `#include "visual/scene/persist.h"` and add these methods:

```cpp
    // ---- Persistence ----

    bool save(const char* path) {
        // Sync viewport state into blueprint before saving
        bp_.pan = vp_.pan;
        bp_.zoom = vp_.zoom;
        bp_.grid_step = vp_.grid_step;
        return save_blueprint_to_file(bp_, path);
    }

    bool load(const char* path) {
        auto bp = load_blueprint_from_file(path);
        if (!bp.has_value()) return false;
        bp_ = std::move(*bp);
        vp_.pan = bp_.pan;
        vp_.zoom = bp_.zoom;
        vp_.grid_step = bp_.grid_step;
        vp_.clamp_zoom();
        cache_.clear();
        return true;
    }
```

### Step 6: Migrate EditorApp from raw `blueprint`/`viewport`/`visual_cache` to `scene.*`

The aliases were already removed. `app.h` has `VisualScene scene;` but `app.cpp` still uses the bare names `blueprint`, `viewport`, `visual_cache` everywhere.

In **app.cpp** do these mechanical replacements:

- `blueprint.` → `scene.blueprint().` (everywhere)
- `viewport.` → `scene.viewport().` (everywhere)
- `visual_cache.` → `scene.cache().` (everywhere)
- `hit_test(blueprint, visual_cache,` → `scene.hitTest(` (for 2-return-param version) or keep as `hit_test(scene.blueprint(), scene.cache(),` — simplest is mechanical replacement
- `hit_test_ports(blueprint, visual_cache,` → `scene.hitTestPorts(`

In **app.h** inline methods:

- `simulation.start(blueprint)` → `simulation.start(scene.blueprint())`

In **examples/an24_editor.cpp**:

- `app.blueprint` → `app.scene.blueprint()`
- `app.viewport` → `app.scene.viewport()`
- `app.visual_cache` → `app.scene.cache()`
- The load/save blocks become `app.scene.load(path)` / `app.scene.save(path)`

In **tests/test_events.cpp**:

- `app.blueprint` → `app.scene.blueprint()`
- `app.viewport` → `app.scene.viewport()`
- `app.visual_cache` → `app.scene.cache()`

### Step 7: Build and fix

```bash
cd /Users/vladimir/an24_cpp/build
cmake .. && make -j4 2>&1 | head -100
```

Fix any remaining include errors. Common issues:

- Relative `../` includes from visual/ subdirectories may need extra `..`
- Some `.cpp` files include their own header via old path

### Step 8: Run tests

```bash
cd /Users/vladimir/an24_cpp/build
ctest --output-on-failure 2>&1 | tail -20
```

All 390 tests must pass (1 pre-existing NOT_BUILT failure is OK).

### Step 9: Commit

```bash
cd /Users/vladimir/an24_cpp
git add -A
git commit -m "Refactor: visual/* directory hierarchy + scene.save()/load()

- Move visual_scene → visual/scene/scene.h
- Move visual_node → visual/node/node.h + node.cpp
- Move visual_port → visual/port/port.h + port.cpp
- Move render, hittest, trigonometry → visual/
- Move widget → visual/node/widget.h + widget.cpp
- Move persist → visual/scene/persist.h + persist.cpp
- Add VisualScene::save() and load() methods
- Migrate EditorApp to use scene.blueprint()/viewport()/cache()
- Update all include paths and CMakeLists.txt
- 390/390 tests passing"
```

## Verification checklist

- [ ] `git status` shows only renamed/modified, no deletions of content
- [ ] No files left in old locations (visual_node.h, visual_port.h, etc. in src/editor/)
- [ ] Build succeeds with 0 errors
- [ ] 390 tests pass
- [ ] `app.blueprint` / `app.viewport` / `app.visual_cache` appear NOWHERE in the codebase
- [ ] `an24_editor.cpp` uses `app.scene.load()` / `app.scene.save()` instead of free functions
