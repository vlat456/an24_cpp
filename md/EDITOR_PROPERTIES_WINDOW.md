# Properties Window - Node Properties Editor

## Overview

A dialog window for editing node properties (name, parameters) via right-click context menu. User right-clicks a node → "Properties" → ImGui window with fields for all `Node::params`. On OK, params are written back to `Node::params` and simulation is rebuilt.

## Invariant: `Node::params` is Always Complete

**`Node::params` is the single source of truth for all component parameters.**

Currently broken: `add_component()` creates nodes with **empty** `params`, and `load_editor_format()` only loads what was saved. The simulator's `merge_device_instance()` papers over this gap at runtime, but the editor data model is incomplete.

**Fix:** Ensure `Node::params` always contains the full merged set (component definition defaults + any user overrides). This is a prerequisite for the properties window — not a UI concern but a data integrity concern shared by editor, JIT, and AOT.

### Current Bug: Params Flow

```
                 EDITOR                              SIMULATOR
                 ======                              =========
add_component()                                 parse_json()
  node.params = {}  ← EMPTY!                      merge_device_instance()
  (defaults NOT copied)                              fills defaults ← OK

load_editor_format()                            blueprint_to_json()
  node.params = JSON only                          writes node.params
  (no merge with registry)                           (may be empty!)

Properties Window                               create_component_variant()
  reads node.params                                get_float(dev, key, default)
  ← SEES NOTHING for new nodes!                     ← uses defaults from DeviceInstance
```

### Target Architecture

```
                 EDITOR                              SIMULATOR
                 ======                              =========
add_component()
  node.params = def->default_params  ← FULL

load_editor_format()
  node.params from JSON
  + apply_params_from_registry()     ← FILL GAPS

blueprint_to_editor_json()
  saves node.params                  ← COMPLETE

Properties Window                               parse_json() → merge_device_instance()
  reads/writes node.params directly                still works, params already complete
  no merging needed in UI layer                    redundant merge is harmless (idempotent)
```

**Principle:** `merge_device_instance()` in `json_parser.cpp` is the canonical merge logic. The editor reuses the same pattern. The UI never merges — it reads/writes `Node::params` directly.

---

## Architecture

### Data Layer (json_parser + persist.cpp)

```
ComponentDefinition::default_params    (components/*.json)
          │
          ▼
┌─────────────────────────────┐
│ apply_params_from_registry()│   New function in persist.cpp
│ (mirrors merge_device_      │   Same pattern as apply_port_types_from_registry()
│  instance() for params)     │
└─────────────┬───────────────┘
              │
              ▼
    Node::params  ← always complete
              │
    ┌─────────┼──────────┐
    │         │          │
    ▼         ▼          ▼
  Editor    Save       Simulator
  (UI)     (JSON)     (JIT/AOT)
```

### UI Layer

```
PropertiesWindow (ImGui::Begin/End window)
├── ImGui::Text         (header: "battery_1 (Battery)")
├── ImGui::InputText    (name field)
├── ImGui::Separator
├── for each node.params[key]:
│   └── ImGui::InputFloat / ImGui::InputText  (param fields)
├── ImGui::Separator
├── ImGui::Button("OK")
└── ImGui::Button("Cancel")
```

**Key decision:** Use ImGui widgets directly, not custom Widget subclasses. Rationale:

- ImGui already handles input, focus, keyboard natively
- Custom IFieldWidget hierarchy adds complexity with zero benefit for a modal dialog
- The existing Widget/IDrawList pattern is for canvas-rendered nodes, not OS-level dialogs
- RowLayout/LabelWidget/ButtonWidget are reimplementing what ImGui does natively
- PropertiesWindow is an ImGui window, not a canvas widget

---

## Context Menu Integration

### Right-Click on Node

Currently, right-click on a node does nothing (only empty-space right-click opens "AddComponent" popup). We need:

1. **`CanvasInput::on_mouse_down(Right)`**: If hit test returns a node → set `show_node_context_menu = true` + `context_menu_node_index`
2. **`InputResult`**: Add `show_node_context_menu` flag and `context_menu_node_index`
3. **`an24_editor.cpp`**: Show popup with "Properties" menu item → opens PropertiesWindow

```cpp
// input_types.h — extend InputResult
struct InputResult {
    bool rebuild_simulation = false;
    bool show_context_menu = false;         // empty-space menu
    Pt context_menu_pos;
    bool show_node_context_menu = false;    // NEW: node right-click menu
    size_t context_menu_node_index = 0;     // NEW: which node
    std::string open_sub_window;
};

// canvas_input.cpp — on_mouse_down, Right button
} else if (btn == MouseButton::Right) {
    HitResult hit = scene_.hitTest(world);
    if (hit.type == HitType::Node) {
        result.show_node_context_menu = true;
        result.context_menu_node_index = hit.node_index;
    } else if (hit.type == HitType::None) {
        result.show_context_menu = true;
        result.context_menu_pos = world;
    }
}
```

---

## PropertiesWindow Class

```cpp
// src/editor/window/properties_window.h
#pragma once

#include "data/node.h"
#include "json_parser/json_parser.h"
#include <functional>
#include <string>
#include <unordered_map>

using PropertyCallback = std::function<void(const std::string& node_id)>;

class PropertiesWindow {
public:
    void open(Node& node, PropertyCallback on_apply);
    void close();
    bool isOpen() const { return open_; }

    /// Call every frame. Returns true while open. Renders ImGui window.
    bool render();

private:
    bool open_ = false;
    Node* target_ = nullptr;
    PropertyCallback on_apply_;

    // Snapshot for cancel/revert
    std::string snapshot_name_;
    std::unordered_map<std::string, std::string> snapshot_params_;

    void applyAndClose();
    void cancelAndClose();
};
```

**Lifecycle:**

1. `open(node, callback)` — takes snapshot of `node.name` and `node.params`
2. `render()` — ImGui window with fields bound to `node.name` / `node.params` directly
3. OK → `on_apply_(node.id)` → caller rebuilds simulation
4. Cancel → restore snapshot → close

---

## EditorApp Integration

```cpp
// app.h — add members
struct EditorApp {
    // ... existing ...
    PropertiesWindow properties_window;

    /// Context menu state for node right-click
    bool show_node_context_menu = false;
    size_t context_menu_node_index = 0;

    void open_properties_for_node(size_t node_index);
};
```

```cpp
// app.cpp
void EditorApp::open_properties_for_node(size_t node_index) {
    if (node_index >= blueprint.nodes.size()) return;
    Node& node = blueprint.nodes[node_index];
    properties_window.open(node, [this](const std::string&) {
        rebuild_simulation();
    });
}
```

```cpp
// an24_editor.cpp — in main loop, after existing context menu

// Node context menu (right-click on node)
if (app.show_node_context_menu) {
    ImGui::OpenPopup("NodeContextMenu");
    app.show_node_context_menu = false;
}
if (ImGui::BeginPopup("NodeContextMenu")) {
    if (ImGui::MenuItem("Properties")) {
        app.open_properties_for_node(app.context_menu_node_index);
    }
    ImGui::EndPopup();
}

// Properties window (renders itself when open)
app.properties_window.render();
```

---

## Implementation Plan

### Phase 0: Params Data Integrity (Prerequisite)

| #   | Task                                                                                    | File          | Test                            |
| --- | --------------------------------------------------------------------------------------- | ------------- | ------------------------------- |
| 0.1 | `add_component()`: copy `def->default_params` into `node.params`                        | `app.cpp`     | `test_add_component_has_params` |
| 0.2 | `apply_params_from_registry()`: fill missing params on load                             | `persist.cpp` | `test_load_merges_params`       |
| 0.3 | Remove `apply_port_types_from_registry()` static registry cache, use passed-in registry | `persist.cpp` | existing tests pass             |

**Tests (TDD):**

```cpp
TEST(ParamsIntegrity, AddComponentPopulatesDefaultParams) {
    // Setup: registry with Battery (6 params)
    // Act: add_component("Battery", ...)
    // Assert: node.params has all 6 keys with default values
}

TEST(ParamsIntegrity, LoadedBlueprintHasFullParams) {
    // Setup: save blueprint with Battery (empty params in JSON)
    // Act: load → apply_params_from_registry
    // Assert: node.params has all 6 Battery keys
}

TEST(ParamsIntegrity, UserOverridesPreservedOnLoad) {
    // Setup: save blueprint with Battery, params: {"v_nominal": "24.0"}
    // Act: load → apply_params_from_registry
    // Assert: node.params["v_nominal"] == "24.0" (override kept)
    // Assert: node.params["internal_r"] == "0.01" (default filled)
}

TEST(ParamsIntegrity, SavedParamsRoundtrip) {
    // Setup: create node, set params, save, load
    // Assert: loaded params == saved params exactly
}
```

### Phase 1: Right-Click Context Menu

| #   | Task                                                                      | File               | Test                               |
| --- | ------------------------------------------------------------------------- | ------------------ | ---------------------------------- |
| 1.1 | Add `show_node_context_menu` + `context_menu_node_index` to `InputResult` | `input_types.h`    | —                                  |
| 1.2 | Right-click on node sets `show_node_context_menu`                         | `canvas_input.cpp` | `test_right_click_node_shows_menu` |
| 1.3 | Add `show_node_context_menu` / `context_menu_node_index` to `EditorApp`   | `app.h`            | —                                  |
| 1.4 | Handle in `apply_input_result()`                                          | `app.h`            | —                                  |
| 1.5 | ImGui popup with "Properties" item                                        | `an24_editor.cpp`  | manual                             |

**Tests (TDD):**

```cpp
TEST(ContextMenu, RightClickOnNode_SetsFlag) {
    // Setup: scene with one node at (100, 100)
    // Act: on_mouse_down(Right) at node position
    // Assert: result.show_node_context_menu == true
    // Assert: result.context_menu_node_index == 0
}

TEST(ContextMenu, RightClickOnEmpty_StillShowsAddMenu) {
    // Setup: scene with one node at (100, 100)
    // Act: on_mouse_down(Right) at empty space (500, 500)
    // Assert: result.show_context_menu == true (existing behavior)
    // Assert: result.show_node_context_menu == false
}
```

### Phase 2: PropertiesWindow (ImGui)

| #   | Task                                                      | File                       | Test                       |
| --- | --------------------------------------------------------- | -------------------------- | -------------------------- |
| 2.1 | `PropertiesWindow` class: `open()`, `close()`, `isOpen()` | `properties_window.h/cpp`  | `test_open_close`          |
| 2.2 | `render()` — ImGui window with name field + param fields  | `properties_window.cpp`    | `test_fields_from_params`  |
| 2.3 | OK → write params back, invoke callback                   | `properties_window.cpp`    | `test_apply_writes_params` |
| 2.4 | Cancel → restore snapshot                                 | `properties_window.cpp`    | `test_cancel_reverts`      |
| 2.5 | Wire into `EditorApp` + `an24_editor.cpp` main loop       | `app.h`, `an24_editor.cpp` | manual                     |

**Tests (TDD):**

```cpp
TEST(PropertiesWindow, OpenSetsTarget) {
    Node n; n.id = "bat1"; n.params = {{"v", "28.0"}};
    PropertiesWindow win;
    win.open(n, [](auto&){});
    EXPECT_TRUE(win.isOpen());
}

TEST(PropertiesWindow, CancelRevertsParams) {
    Node n; n.id = "bat1"; n.params = {{"v", "28.0"}};
    PropertiesWindow win;
    win.open(n, [](auto&){});
    n.params["v"] = "12.0";  // user edits
    win.close();  // cancel
    EXPECT_EQ(n.params["v"], "28.0");  // reverted
}

TEST(PropertiesWindow, ApplyKeepsChangesAndCallsCallback) {
    Node n; n.id = "bat1"; n.params = {{"v", "28.0"}};
    bool called = false;
    PropertiesWindow win;
    win.open(n, [&](const std::string& id) { called = true; });
    n.params["v"] = "12.0";
    // simulate OK click (call applyAndClose internally)
    // EXPECT_EQ(n.params["v"], "12.0");
    // EXPECT_TRUE(called);
}
```

### Phase 3: Simulation Rebuild on Apply

| #   | Task                                             | File      | Test                            |
| --- | ------------------------------------------------ | --------- | ------------------------------- |
| 3.1 | `on_apply` callback calls `rebuild_simulation()` | `app.cpp` | integration test                |
| 3.2 | Verify changed params take effect in sim         | —         | `test_param_change_affects_sim` |

**Tests (TDD):**

```cpp
TEST(ParamSimIntegration, ChangedFactorAffectsLerpOutput) {
    // Setup: circuit with LerpNode, factor="0.05"
    // Act: change params["factor"] = "0.5", rebuild sim, step
    // Assert: output converges 10x faster than before
}
```

---

## File Structure

```
src/editor/
├── input/
│   └── input_types.h           (extend InputResult)
│   └── canvas_input.cpp        (right-click on node)
├── window/
│   ├── properties_window.h     (new)
│   └── properties_window.cpp   (new)
├── visual/scene/
│   └── persist.cpp             (apply_params_from_registry)
├── app.h                       (add PropertiesWindow member)
├── app.cpp                     (add_component params fix)

tests/
├── test_params_integrity.cpp   (new — Phase 0 tests)
├── test_context_menu.cpp       (extend — Phase 1 tests)
├── test_properties_window.cpp  (new — Phase 2 tests)

examples/
└── an24_editor.cpp             (node context menu + render)
```

---

## Design Decisions & Rationale

| Decision                    | Choice                                        | Rationale                                                                                                                  |
| --------------------------- | --------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| Params always complete      | `Node::params` = defaults + overrides         | Single source of truth for editor, save, JIT, AOT. No lazy merging.                                                        |
| Merge at data layer         | `apply_params_from_registry()` in persist.cpp | Mirrors `apply_port_types_from_registry()` pattern. Same merge logic as `merge_device_instance()`.                         |
| ImGui native widgets        | `ImGui::InputFloat` / `InputText`             | PropertiesWindow is a dialog, not a canvas widget. No need for custom RowLayout/IFieldWidget.                              |
| No custom Widget subclasses | Skip LabelWidget/ButtonWidget/etc             | YAGNI. The existing Widget hierarchy is for node rendering on canvas. Modal dialogs should use ImGui directly.             |
| Right-click on node         | Extend `InputResult` + node context menu      | Natural UX. Same pattern as existing empty-space right-click.                                                              |
| Snapshot for cancel         | Copy params on `open()`, restore on cancel    | Simple, correct. No need for undo stack integration.                                                                       |
| All params are strings      | `unordered_map<string, string>`               | Matches existing `ComponentDefinition::default_params` and `DeviceInstance::params`. Type conversion happens in simulator. |

---

## Testing Strategy (TDD Order)

```
Phase 0: Data integrity tests FIRST
  test_params_integrity.cpp
    ├── AddComponentPopulatesDefaultParams
    ├── LoadedBlueprintHasFullParams
    ├── UserOverridesPreservedOnLoad
    └── SavedParamsRoundtrip

Phase 1: Context menu tests
  test_context_menu.cpp
    ├── RightClickOnNode_SetsFlag
    └── RightClickOnEmpty_StillShowsAddMenu

Phase 2: Properties window unit tests
  test_properties_window.cpp
    ├── OpenSetsTarget
    ├── CancelRevertsParams
    └── ApplyKeepsChangesAndCallsCallback

Phase 3: Integration test
  test_params_integrity.cpp (extend)
    └── ChangedFactorAffectsLerpOutput
```

---

## Success Criteria

- [ ] `Node::params` is never empty for known component types
- [ ] `add_component()` populates params from `ComponentDefinition::default_params`
- [ ] `load_editor_format()` merges params with registry (fills gaps, preserves overrides)
- [ ] Right-click on node → "Properties" menu item
- [ ] Properties window shows all params as editable fields
- [ ] OK applies changes to `Node::params` and rebuilds simulation
- [ ] Cancel reverts to snapshot
- [ ] Changed params take effect in running simulation
- [ ] Save/load roundtrip preserves all params
- [ ] All unit tests pass
- [ ] No regressions in existing editor/simulation functionality

---

## References

- `merge_device_instance()`: `src/json_parser/json_parser.cpp:764` — canonical merge logic
- `apply_port_types_from_registry()`: `src/editor/visual/scene/persist.cpp` — pattern to follow for params
- `add_component()`: `src/editor/app.cpp:66` — needs params fix
- `load_editor_format()`: `src/editor/visual/scene/persist.cpp:433` — needs params merge
- `CanvasInput::on_mouse_down()`: `src/editor/input/canvas_input.cpp:164` — right-click handling
- `InputResult`: `src/editor/input/input_types.h:47` — extend for node menu
- Context menu: `examples/an24_editor.cpp:552` — add node popup
