# Task: Create ui::RenderContext

## Objective
Create `src/ui/renderer/render_context.h` - a generic render context without domain-specific pointers.

## Files to Create

### src/ui/renderer/render_context.h

```cpp
#pragma once
#include "ui/math/pt.h"
#include <cstdint>

namespace ui {

struct RenderContext {
    float dt = 0.0f;
    float zoom = 1.0f;
    Pt pan{0, 0};
    Pt canvas_min{0, 0};
    Pt mouse_pos{0, 0};
    
    uint64_t hovered_id = 0;
    uint64_t selected_id = 0;
    
    bool is_dragging = false;
    
    Pt world_to_screen(Pt world) const {
        return Pt((world.x - pan.x) * zoom + canvas_min.x,
                  (world.y - pan.y) * zoom + canvas_min.y);
    }
};

} // namespace ui
```

## CMake Update

Add to `tests/CMakeLists.txt` after `ui_grid_tests`:

```cmake
# UI RenderContext tests (Phase 3.2 of UI Library Extraction)
add_executable(ui_render_context_tests
    test_ui_render_context.cpp
)
target_include_directories(ui_render_context_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui_render_context_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(ui_render_context_tests)
```

## Verification

After creating the file:
1. Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug`
2. Run: `cmake --build build --target ui_render_context_tests -j8`
3. Run: `cd build/tests && ./ui_render_context_tests`
4. All 4 tests should pass.
