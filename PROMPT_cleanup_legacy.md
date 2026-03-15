# Legacy Cleanup: CMakeLists + Broken Includes

Mechanical cleanup task. Follow each section exactly. Do not add new code beyond what's specified.

---

## 1. Create `src/editor/visual/snap.h` (new file)

The deleted `visual/trigonometry.h` contained `editor_math::snap_to_grid` which is still used by 3 source files. Create a minimal replacement:

```cpp
#pragma once

#include "data/pt.h"
#include <cmath>

namespace editor_math {

inline Pt snap_to_grid(Pt pos, float grid_step) {
    return Pt(
        std::round(pos.x / grid_step) * grid_step,
        std::round(pos.y / grid_step) * grid_step
    );
}

} // namespace editor_math
```

---

## 2. Fix broken `#include` in source files

### `src/editor/document.cpp`

Replace lines 1-12:

```cpp
#include "document.h"
#include "visual/scene_mutations.h"
#include "visual/persist.h"
#include "visual/trigonometry.h"
#include "visual/node/node.h"
#include "debug.h"
#include "data/wire.h"
#include "data/node.h"
#include "json_parser/json_parser.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <iostream>
```

With:

```cpp
#include "document.h"
#include "visual/scene_mutations.h"
#include "visual/persist.h"
#include "visual/snap.h"
#include "debug.h"
#include "data/wire.h"
#include "data/node.h"
#include "json_parser/json_parser.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <iostream>
```

(Removed `"visual/trigonometry.h"` and `"visual/node/node.h"`, added `"visual/snap.h"`)

### `src/editor/app.cpp`

Replace:
```cpp
#include "visual/trigonometry.h"
```
With:
```cpp
#include "visual/snap.h"
```

### `src/editor/input/canvas_input.cpp`

Replace:
```cpp
#include "visual/trigonometry.h"
```
With:
```cpp
#include "visual/snap.h"
```

### `src/editor/visual/renderer/node_frame.cpp`

Replace:
```cpp
#include "visual/port/port.h"
```
With:
```cpp
#include "visual/port/visual_port.h"
```

Also in the same file, the function `render_ports` uses `port.worldPosition()` and `port.type()`. The new `visual::Port` class has the same methods, but lives in `namespace visual`. Update the function signature at line 17-18 from:

```cpp
void render_ports(IDrawList& dl, const Viewport& vp, Pt canvas_min,
                  const std::vector<VisualPort>& ports) {
```

To:

```cpp
void render_ports(IDrawList& dl, const Viewport& vp, Pt canvas_min,
                  const std::vector<visual::Port>& ports) {
```

### `src/editor/visual/renderer/node_frame.h`

Replace the forward declaration at line 9:
```cpp
class VisualPort;
```
With:
```cpp
namespace visual { class Port; }
```

Replace the function declaration at lines 24-25:
```cpp
void render_ports(IDrawList& dl, const Viewport& vp, Pt canvas_min,
                  const std::vector<VisualPort>& ports);
```
With:
```cpp
void render_ports(IDrawList& dl, const Viewport& vp, Pt canvas_min,
                  const std::vector<visual::Port>& ports);
```

### `src/editor/visual/renderer/port_layout_builder.h`

Replace lines 1-6:
```cpp
#pragma once

#include "visual/node/widget/widget_base.h"
#include "visual/node/widget/containers/column.h"
#include "json_parser/json_parser.h"
#include <memory>
```
With:
```cpp
#pragma once

#include "json_parser/json_parser.h"
#include <memory>
```

The types `Widget`, `Column`, `PortSlot` used in this header are now orphaned (no callers). But to keep it compilable, also add forward declarations after the includes:

```cpp
class Widget;
class Column;
```

**Note:** `port_layout_builder.h/.cpp` is dead code (no callers). Alternatively, you can just delete both files:
- `src/editor/visual/renderer/port_layout_builder.h`
- `src/editor/visual/renderer/port_layout_builder.cpp`

If you delete them, also remove them from `examples/CMakeLists.txt` (the `an24_editor` target does NOT list them, so no change needed there — but verify).

**Recommended:** Delete `port_layout_builder.h` and `port_layout_builder.cpp`. They are dead code.

---

## 3. Delete dead renderer files

These files are dead code (no callers in surviving source):

- `src/editor/visual/renderer/port_layout_builder.h`
- `src/editor/visual/renderer/port_layout_builder.cpp`

(node_frame IS still potentially reusable, keep it for now)

---

## 4. Clean `tests/CMakeLists.txt` — Remove legacy test targets

Delete the ENTIRE `add_executable(...)` block + `target_*` + `gtest_discover_tests()` for each of these targets. The test source files for these have already been deleted:

1. **`editor_persist_tests`** (lines 313-362) — test_persist.cpp deleted
2. **`editor_render_tests`** (lines 392-438) — test_render.cpp deleted
3. **`editor_hittest_tests`** (lines 440-489) — test_hittest.cpp deleted
4. **`editor_router_tests`** (lines 554-599) — test_router.cpp deleted
5. **`editor_widget_tests`** (lines 601-649) — test_widget.cpp, test_bus_port_swap.cpp, test_spatial_grid.cpp deleted
6. **`editor_hierarchical_tests`** (lines 1087-1133) — test_editor_hierarchical.cpp deleted
7. **`visual_port_tests`** (lines 1136-1181) — test_visual_port.cpp deleted
8. **`visual_scene_tests`** (lines 1183-1233) — test_visual_scene.cpp deleted
9. **`refactoring_regression_tests`** (lines 1236-1285) — test_refactoring_regression.cpp deleted
10. **`node_color_tests`** (lines 1689-1734) — test_node_color.cpp deleted
11. **`node_autosize_tests`** (lines 1737-1782) — test_node_autosize.cpp deleted
12. **`layout_tests`** (lines 1784-1808) — test_layout.cpp deleted
13. **`wire_module_tests`** (lines 2081-2121) — test_wire_modules.cpp deleted
14. **`context_menu_tests`** (lines 1522-1594) — test_context_menu.cpp deleted

---

## 5. Clean `tests/CMakeLists.txt` — Fix surviving test targets

These test targets have surviving `.cpp` test files but reference deleted source files in their `add_executable()`. Remove the deleted source lines. Here is the EXACT list of source lines to REMOVE from each target:

### Lines to REMOVE (they reference deleted files):

These source file paths no longer exist. Remove every line referencing any of them from ALL surviving test targets:

```
${CMAKE_SOURCE_DIR}/src/editor/visual/scene/wire_manager.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/scene/scene.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/hittest/nodes.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/hittest/ports.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/types/bus_node.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/types/ref_node.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/types/group_node.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/types/text_node.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node_cache.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/widget_base.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/primitives/label.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/primitives/circle.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/primitives/spacer.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/containers/container.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/content/header_widget.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/content/type_name_widget.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/content/switch_widget.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/content/vertical_toggle.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/content/voltmeter_widget.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_frame.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/port_layout_builder.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire/polyline_builder.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire/polyline_draw.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire/arc_draw.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
```

Also remove any comments like `# Legacy visual (still needed for some transitive deps)` or `# Legacy visual (still needed for VisualScene in SwapWirePortsOnBus tests)` that precede these deleted source blocks.

### Affected surviving test targets:

1. **`inspector_tests`** — Remove all 31 deleted source paths listed above, plus `${CMAKE_SOURCE_DIR}/src/editor/visual/scene/wire_manager.cpp`. Also remove `${CMAKE_SOURCE_DIR}/src/editor/visual/scene/scene.cpp`. Keep: `test_inspector.cpp`, `../src/editor/visual/inspector/inspector_core.cpp`, `${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp`, `${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp`, `${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp`.

2. **`node_deletion_tests`** — Remove all deleted source paths. Keep: `test_node_deletion.cpp`, `${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp`, and all the new-system sources (scene.cpp, widget.cpp, grid.cpp, scene_mutations.cpp, scene_hittest.cpp, wire/wire.cpp, wire/wire_end.cpp, port/visual_port.cpp, node/visual_node.cpp, node/group_node_widget.cpp, node/bus_node_widget.cpp, node/ref_node_widget.cpp, node/text_node_widget.cpp, primitives/primitives.cpp, widgets/content_widgets.cpp, data/blueprint.cpp).

3. **`multi_window_tests`** — Same as node_deletion_tests pattern. Remove all deleted source paths. Keep new-system sources.

4. **`params_integrity_tests`** — Same pattern. Remove all deleted source paths. Keep new-system sources plus `${CMAKE_SOURCE_DIR}/src/editor/app.cpp`, `${CMAKE_SOURCE_DIR}/src/editor/window/properties_window.cpp`, `${CMAKE_SOURCE_DIR}/src/editor/visual/inspector/inspector_core.cpp`.

5. **`document_window_system_tests`** — Same pattern. Remove all deleted source paths. Keep new-system sources plus `${CMAKE_SOURCE_DIR}/src/editor/document.cpp`, `${CMAKE_SOURCE_DIR}/src/editor/window_system.cpp`, `${CMAKE_SOURCE_DIR}/src/editor/window/properties_window.cpp`, `${CMAKE_SOURCE_DIR}/src/editor/visual/inspector/inspector_core.cpp`.

For each target, also remove `${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp` and `${CMAKE_SOURCE_DIR}/src/editor/visual/scene/scene.cpp` references **ONLY IF** those files no longer exist. `visual/persist.cpp` DOES still exist — keep it. `visual/scene/scene.cpp` does NOT exist — remove it.

---

## 6. Clean `examples/CMakeLists.txt` — Fix `an24_editor` target

Remove these lines from the `an24_editor` `add_executable()` block (they reference deleted files):

```
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/wire_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/hittest/nodes.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/hittest/ports.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/types/bus_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/types/ref_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/types/group_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/types/text_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/widget_base.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/primitives/label.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/primitives/circle.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/primitives/spacer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/containers/container.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/content/header_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/content/type_name_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/content/switch_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/content/vertical_toggle.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget/content/voltmeter_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire/polyline_builder.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire/polyline_draw.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire/arc_draw.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_frame.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/port_layout_builder.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/scene.cpp
```

**Keep** these lines (they reference files that still exist):
```
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp   # still exists, used by canvas_renderer
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp                  # still exists
    ${CMAKE_SOURCE_DIR}/src/editor/visual/canvas_renderer.cpp          # still exists
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node_content_renderer.cpp  # still exists
```

---

## 7. Verification

After all changes, run:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug 2>&1 | head -50
cmake --build build -j$(nproc) 2>&1 | tail -100
```

Expected: CMake configure succeeds, build completes (possibly with warnings but no errors about missing files).

Then run the surviving new-system tests:

```bash
cd build && ctest --output-on-failure 2>&1 | tail -50
```

---

## Summary of changes

| Action | Count |
|--------|-------|
| New file created | 1 (`visual/snap.h`) |
| Source files with fixed includes | 5 (`document.cpp`, `app.cpp`, `canvas_input.cpp`, `node_frame.cpp`, `node_frame.h`) |
| Dead source files deleted | 2 (`port_layout_builder.h/.cpp`) |
| Test targets removed from CMake | 14 |
| Test targets cleaned (legacy sources removed) | 5 |
| `an24_editor` target cleaned | ~30 deleted source lines |
