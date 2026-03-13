# UI Library Extraction Plan

**Objective**: Refactor `visual::` namespace into a standalone, generic `ui::` library in `src/ui/`.

**Principles**:
- Everything is a Widget
- Strict decoupling (zero domain knowledge in `src/ui/`)
- Failing-test-first for every change
- No backward compatibility

---

## Phase 1: Foundation Primitives

### 1.1 Move `Pt` to `src/ui/math/`

**Failing Test**: Compilation error - `ui::Pt` does not exist yet.

```cpp
// tests/test_ui_math.cpp
#include "ui/math/pt.h"

TEST(UIMath, PtExists) {
    ui::Pt p{10.0f, 20.0f};
    EXPECT_FLOAT_EQ(p.x, 10.0f);
    EXPECT_FLOAT_EQ(p.y, 20.0f);
}
```

**Mechanical Task Prompt**:
```
Move src/editor/data/pt.h to src/ui/math/pt.h
- Change namespace from (current) to ui
- Remove any domain-specific includes
- Update all #include paths across the entire codebase
- Run: grep -r "editor/data/pt.h" and fix all references
```

**Verification**: Test compiles and passes.

---

### 1.2 Move `Edges` to `src/ui/layout/`

**Failing Test**: Compilation error - `ui::Edges` does not exist.

```cpp
// tests/test_ui_layout.cpp
#include "ui/layout/edges.h"

TEST(UILayout, EdgesExist) {
    ui::Edges e{5.0f, 10.0f, 5.0f, 10.0f};
    EXPECT_FLOAT_EQ(e.left, 5.0f);
    EXPECT_FLOAT_EQ(e.top, 10.0f);
}
```

**Mechanical Task Prompt**:
```
Move src/editor/visual/node/edges.h to src/ui/layout/edges.h
- Change namespace from visual to ui
- Update Pt references to ui::Pt
- Update all #include paths across the codebase
```

**Verification**: Test compiles and passes.

---

### 1.3 Create `IDrawList` Interface

**Failing Test**: Compilation error - `ui::IDrawList` does not exist.

```cpp
// tests/test_ui_renderer.cpp
#include "ui/renderer/idraw_list.h"

TEST(UIRenderer, IDrawListExists) {
    // Should be an abstract interface
    struct MockDrawList : ui::IDrawList {
        void drawRect(ui::Pt, ui::Pt, uint32_t) override {}
        void drawText(ui::Pt, const char*, uint32_t) override {}
        void drawLine(ui::Pt, ui::Pt, uint32_t, float) override {}
    };
    MockDrawList dl;
    SUCCEED();
}
```

**Mechanical Task Prompt**:
```
Create src/ui/renderer/idraw_list.h
- Extract drawing interface from visual::Scene or visual::Widget
- Methods: drawRect, drawText, drawLine, drawCircle, drawPoly
- All coordinates use ui::Pt
- Colors use uint32_t (0xRRGGBBAA)
- Pure virtual interface, no implementation
```

**Verification**: Test compiles.

---

## Phase 2: Core Widget Purification

### 2.1 Create Pure `ui::Widget` Base

**Failing Test**: Domain methods should not exist on base widget.

```cpp
// tests/test_ui_widget_purity.cpp
#include "ui/core/widget.h"

TEST(UIWidget, NoDomainMethods) {
    // These should NOT compile on base ui::Widget:
    // widget.updateFromContent() - domain-specific
    // widget.portByName("x") - domain-specific
    // widget.customColor() - domain-specific
    
    // These SHOULD exist:
    ui::Widget w;
    w.setPos(ui::Pt{0,0});
    w.setSize(ui::Pt{100,100});
    EXPECT_TRUE(w.contains(ui::Pt{50,50}));
}
```

**Mechanical Task Prompt**:
```
Create src/ui/core/widget.h and widget.cpp
- Copy from src/editor/visual/widget.h
- REMOVE these methods:
  - updateFromContent()
  - portByName()
  - ports()
  - customColor()
  - Any reference to Node, Wire, Port, or domain types
- KEEP these methods:
  - setPos/getPos
  - setSize/getSize
  - contains()
  - addChild/removeChild
  - render() - pure virtual
  - hitTest()
- Change namespace to ui
- Use ui::Pt and ui::Edges
```

**Verification**: Test compiles, domain-specific method calls fail to compile.

---

### 2.2 Create `ui::Scene` Orchestrator

**Failing Test**: Scene should not know about domain types.

```cpp
// tests/test_ui_scene.cpp
#include "ui/core/scene.h"

TEST(UIScene, NoDomainTypes) {
    ui::Scene scene;
    
    // Should work:
    scene.addWidget(std::make_unique<ui::Widget>());
    scene.removeWidget(widget_id);
    scene.render(drawList);
    
    // Should NOT exist:
    // scene.addNode() - no
    // scene.wires() - no
    // scene.connect() - no
}
```

**Mechanical Task Prompt**:
```
Create src/ui/core/scene.h and scene.cpp
- Copy from src/editor/visual/scene.h
- REMOVE all domain-specific logic:
  - Node creation/deletion
  - Wire routing
  - Port management
  - JSON parsing for nodes
- KEEP generic scene logic:
  - Widget tree management
  - Hit testing
  - Z-order sorting
  - Render dispatch
- Change namespace to ui
```

**Verification**: Test compiles.

---

### 2.3 Create `ui::Grid` Spatial Hash

**Failing Test**: Grid should work with generic bounds.

```cpp
// tests/test_ui_grid.cpp
#include "ui/core/grid.h"

TEST(UIGrid, GenericSpatialHash) {
    ui::Grid grid(32.0f); // cell size
    
    ui::Pt min{0,0}, max{100,100};
    grid.insert(entity_id, min, max);
    
    auto results = grid.query(ui::Pt{50,50}, ui::Pt{60,60});
    EXPECT_EQ(results.size(), 1);
}
```

**Mechanical Task Prompt**:
```
Move src/editor/visual/grid.h to src/ui/core/grid.h
- Template or generalize to work with any entity ID type
- Remove domain-specific includes
- Change namespace to ui
```

**Verification**: Test compiles and passes.

---

## Phase 3: Render Abstraction

### 3.1 Replace `RenderLayer` Enum with Numeric Z-Order

**Failing Test**: Enum should not exist in ui:: namespace.

```cpp
// tests/test_ui_zorder.cpp
#include "ui/core/widget.h"

TEST(UIWidget, NumericZOrder) {
    ui::Widget w;
    w.setZOrder(1.5f);
    EXPECT_FLOAT_EQ(w.zOrder(), 1.5f);
    
    // Should NOT compile:
    // w.setLayer(RenderLayer::Background)
}
```

**Mechanical Task Prompt**:
```
In src/ui/core/widget.h:
- Replace any RenderLayer enum usage with float z_order
- Add setZOrder(float) and zOrder() methods
- Default z_order = 0.0f
- Update render sorting to use float comparison
```

**Verification**: Test compiles and passes.

---

### 3.2 Create `ui::RenderContext`

**Failing Test**: RenderContext should not hold domain pointers.

```cpp
// tests/test_ui_render_context.cpp
#include "ui/renderer/render_context.h"

TEST(UIRenderContext, NoDomainPointers) {
    ui::RenderContext ctx;
    
    // Should work:
    ctx.dt = 0.016f;
    ctx.hovered_id = 42;
    ctx.selected_id = 100;
    
    // Should NOT exist:
    // ctx.wire - no
    // ctx.routingPoint - no
    // ctx.node - no
}
```

**Mechanical Task Prompt**:
```
Create src/ui/renderer/render_context.h
- Fields:
  - float dt (delta time)
  - uint64_t hovered_id
  - uint64_t selected_id
  - ui::IDrawList* draw_list
  - ui::Pt mouse_pos
  - bool is_dragging
- NO domain pointers (Wire*, Node*, Port*, etc.)
```

**Verification**: Test compiles.

---

## Phase 4: Layout System

### 4.1 Move `LinearLayout` to `src/ui/layout/`

**Failing Test**: LinearLayout should work with ui::Widget.

```cpp
// tests/test_ui_linear_layout.cpp
#include "ui/layout/linear_layout.h"
#include "ui/core/widget.h"

TEST(UILayout, LinearLayout) {
    ui::LinearLayout layout;
    layout.setDirection(ui::LinearLayout::Horizontal);
    layout.setSpacing(10.0f);
    layout.setPadding(ui::Edges{5,5,5,5});
    
    ui::Widget container;
    layout.apply(container);
}
```

**Mechanical Task Prompt**:
```
Move linear layout logic from src/editor/visual/ to src/ui/layout/linear_layout.h
- Change namespace to ui
- Operate on ui::Widget base class
- Remove domain-specific assumptions
```

**Verification**: Test compiles.

---

## Phase 5: Domain Layer Integration

### 5.1 Create `editor::NodeWidget` extending `ui::Widget`

**Failing Test**: NodeWidget should be in editor namespace.

```cpp
// tests/test_editor_node_widget.cpp
#include "editor/visual/node/node_widget.h"

TEST(EditorNodeWidget, ExtendsUIWidget) {
    editor::NodeWidget nw;
    ui::Widget* base = &nw; // Should compile
    
    // Domain methods exist here:
    nw.updateFromContent();
    nw.portByName("input");
}
```

**Mechanical Task Prompt**:
```
Create src/editor/visual/node/node_widget.h
- Inherit from ui::Widget
- Add back domain-specific methods:
  - updateFromContent()
  - portByName()
  - ports()
  - customColor()
- Keep existing Node, Port logic
```

**Verification**: Test compiles.

---

### 5.2 Create `editor::EditorScene` extending `ui::Scene`

**Failing Test**: EditorScene should have domain methods.

```cpp
// tests/test_editor_scene.cpp
#include "editor/visual/editor_scene.h"

TEST(EditorScene, HasDomainMethods) {
    editor::EditorScene scene;
    
    // ui::Scene methods work:
    scene.addWidget(...);
    scene.render(...);
    
    // Domain methods exist:
    scene.addNode(config);
    scene.connect(port_a, port_b);
    scene.wires();
}
```

**Verification**: Test compiles.

---

## Phase 6: Cleanup

### 6.1 Remove Old `visual::` Files

**After all migrations complete:**

```bash
# Remove empty/migrated files from src/editor/visual/
# Keep only domain-specific: node/, wire/, port/
```

### 6.2 Update CMake

**Task**: Add `src/ui/` to build system, remove old references.

---

## Progress Tracking

| Phase | Task | Status |
|-------|------|--------|
| 1.1 | Move Pt | ✅ Complete |
| 1.2 | Move Edges | ✅ Complete |
| 1.3 | Create IDrawList | ✅ Complete |
| 2.1 | Pure ui::Widget | ✅ Complete |
| 2.2 | ui::Scene | ✅ Complete |
| 2.3 | ui::Grid | ✅ Complete |
| 3.1 | Numeric Z-Order | ✅ Complete (done in 2.1) |
| 3.2 | ui::RenderContext | ✅ Complete |
| 4.1 | LinearLayout | ✅ Complete |
| 5.1 | editor::NodeWidget | ⬜ Pending |
| 5.2 | editor::EditorScene | ⬜ Pending |
| 6.1 | Cleanup old files | ⬜ Pending |
| 6.2 | Update CMake | ⬜ Pending |

---

## Notes for Simple AI

Each "Mechanical Task Prompt" is designed to be:
1. Self-contained
2. Verifiable by compilation
3. Non-destructive (creates new files first)

After each mechanical task, I will verify the result before proceeding.
