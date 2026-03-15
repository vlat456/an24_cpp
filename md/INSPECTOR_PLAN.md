# Inspector Implementation Plan

## Overview

Flat tree view of blueprint components with ports and connections. Uses ImGui native widgets (not canvas IDrawList rendering). Dockable left panel in editor.

---

## Architecture (Following src/editor/visual Style)

### Location: `src/editor/visual/inspector/`

```
inspector/
├── inspector.h          (IInspectable, Inspector classes)
├── inspector.cpp        (implementation)
└── port_connection.h    (PortConnection struct)
```

### Design Patterns (from existing code)

| Pattern | Source | Usage |
|----------|--------|-------|
| Non-owning reference | `WireManager` | `Inspector(VisualScene&)` |
| Const query methods | `WireManager::wireEndPosition()` | `findConnection(node, port)` |
| Struct for result | `WirePortMatch` | `PortConnection` |
| Forward declarations | `class VisualScene;` | Avoid unnecessary includes |

---

## Data Structures

### `port_connection.h` (NEW)

```cpp
#pragma once

#include <string>
#include <optional>

/// Result of finding what connects to a port
struct PortConnection {
    std::string node_id;      // "main_battery"
    std::string port_name;    // "v_out"
    std::string display;      // "Battery.v_out"
    size_t wire_index = 0;    // For double-click to select wire
};
```

---

## Class Design

### `inspector.h`

```cpp
#pragma once

#include "port_connection.h"
#include "data/pt.h"
#include "data/port.h"
#include "data/node.h"
#include <string>
#include <vector>
#include <unordered_map>

class VisualScene;

/// Inspector - renders component tree with port connections
/// Uses ImGui native widgets (not IDrawList/canvas rendering)
class Inspector {
public:
    explicit Inspector(VisualScene& scene);

    // ---- Rendering ----

    /// Render inspector window (ImGui::Begin/End handled by caller)
    void render();

    // ---- Queries ----

    /// Find what connects to a port (const query)
    std::optional<PortConnection> findConnection(
        const std::string& node_id,
        const std::string& port_name,
        PortSide side) const;

    /// Count connections for a node
    size_t countConnections(const Node& node) const;

    // ---- State ----

    bool show_search = true;
    bool show_ports = true;
    char search_buffer[128] = "";

private:
    VisualScene& scene_;

    // Rendering helpers
    void renderNode(const Node& node);
    void renderPortRow(const Node& node, const Port& port, PortSide side);
    void renderInputs(const Node& node);
    void renderOutputs(const Node& node);

    // Filtering
    bool passesFilter(const Node& node) const;

    // Sort mode
    enum class SortMode { Name, Type, Connections };
    SortMode sort_mode_ = SortMode::Name;
};
```

---

## Implementation Plan (TDD)

### Phase 0: Data Structure (0.5h)

| Task | File | Test |
|------|------|------|
| Create `port_connection.h` | `inspector/port_connection.h` | — |
| Add to CMakeLists | `CMakeLists.txt` | — |

---

### Phase 1: Connection Lookup (2h)

| Task | File | Test |
|------|------|------|
| `Inspector::findConnection()` | `inspector.cpp` | `test_inspector.cpp` |
| `Inspector::countConnections()` | `inspector.cpp` | `test_inspector.cpp` |

**Tests (TDD):**

```cpp
TEST(Inspector, FindConnection_InputPort) {
    // Setup: blueprint with Battery -> Switch
    // Act: findConnection("switch", "v_in", Input)
    // Assert: returns Battery.v_out
}

TEST(Inspector, FindConnection_OutputPort) {
    // Setup: blueprint with Battery -> Switch
    // Act: findConnection("battery", "v_out", Output)
    // Assert: returns Switch.v_in
}

TEST(Inspector, FindConnection_NotConnected) {
    // Setup: port with no wire
    // Act: findConnection("battery", "unused", Output)
    // Assert: returns std::nullopt
}

TEST(Inspector, CountConnections_NodeWithWires) {
    // Setup: Switch with 2 wires
    // Act: countConnections(switch)
    // Assert: returns 2
}
```

---

### Phase 2: Node Rendering (3h)

| Task | File | Test |
|------|------|------|
| `renderNode()` - TreeNode | `inspector.cpp` | Manual (ImGui) |
| `renderPortRow()` - connection display | `inspector.cpp` | Manual (ImGui) |
| `renderInputs()` - Inputs subtree | `inspector.cpp` | Manual (ImGui) |
| `renderOutputs()` - Outputs subtree | `inspector.cpp` | Manual (ImGui) |

**Note:** ImGui rendering is tested manually (not unit tests). Use visual verification.

---

### Phase 3: Filtering & Sorting (2h)

| Task | File | Test |
|------|------|------|
| `passesFilter()` - search by name/type | `inspector.cpp` | `test_inspector.cpp` |
| Sort nodes by Name/Type/Connections | `inspector.cpp` | `test_inspector.cpp` |

**Tests:**

```cpp
TEST(Inspector, FilterByNodeName) {
    // Setup: scene with Battery (main_battery), Lamp
    // Act: search_buffer = "main"
    // Assert: only Battery passes filter
}

TEST(Inspector, FilterByTypeName) {
    // Setup: scene with Battery, Switch
    // Act: search_buffer = "Battery"
    // Assert: only Battery passes filter
}

TEST(Inspector, SortByName) {
    // Setup: nodes [Switch, Battery, Lamp]
    // Act: sort_mode_ = Name
    // Assert: [Battery, Lamp, Switch] (alphabetical)
}

TEST(Inspector, SortByConnections) {
    // Setup: Battery(1 wire), Switch(2 wires), Lamp(1 wire)
    // Act: sort_mode_ = Connections
    // Assert: [Switch, Battery, Lamp] (descending)
}
```

---

### Phase 4: EditorApp Integration (1h)

| Task | File | Test |
|------|------|------|
| Add `Inspector inspector_` to EditorApp | `app.h` | — |
| Add `show_inspector` flag | `app.h` | — |
| Wire up main loop | `an24_editor.cpp` | Manual |

---

### Phase 5: Docking Layout (1h)

| Task | File | Test |
|------|------|------|
| Add SetupInspectorLayout() | `an24_editor.cpp` | Manual |
| Add "Reset Layout" menu item | `an24_editor.cpp` | Manual |
| Configure dock nodes (Left | Center | Right) | `an24_editor.cpp` | Manual |

---

## File Structure

```
src/editor/visual/inspector/
├── port_connection.h     (struct PortConnection)
├── inspector.h            (class Inspector)
└── inspector.cpp          (implementation)

src/editor/
├── app.h                  (add Inspector inspector_)
└── app.cpp                (initialize inspector_)

examples/
└── an24_editor.cpp         (SetupInspectorLayout(), render)

tests/
└── test_inspector.cpp      (NEW - unit tests)

CMakeLists.txt              (add inspector files, test target)
```

---

## Dependencies

### Internal
- `VisualScene` - access to blueprint, nodes, wires
- `Node` - node data (name, type_name, inputs, outputs)
- `Port` - port data (name, side, type)
- `Wire` - connection data

### External
- `ImGui` - native widgets (TreeNode, Selectable, Text, etc.)

---

## Success Criteria

- [ ] Flat list of all components (not hierarchical by category)
- [ ] Each component expands to show Inputs/Outputs
- [ ] Each port shows connected node:port (or "[not connected]")
- [ ] Search/filter by node name or type
- [ ] Sort by Name/Type/Connections
- [ ] Double-click on connection → focus connected node
- [ ] Dockable left panel (drag/undock)
- [ ] All unit tests pass
- [ ] No regressions in existing functionality

---

## Implementation Order (TDD)

```
1. Create files + CMakeLists (15 min)
   ├─ port_connection.h
   ├─ inspector.h (empty)
   ├─ inspector.cpp (empty)
   └─ test_inspector.cpp (empty)

2. Write FAILING tests (30 min)
   ├─ TEST(FindConnection_InputPort)
   ├─ TEST(FindConnection_NotConnected)
   └─ TEST(CountConnections)

3. Implement findConnection() (30 min)
   └─ Tests PASS ✓

4. Implement renderNode() (1 hour)
   └─ Manual ImGui verification

5. Implement filtering (30 min)
   └─ TEST passes ✓

6. Implement sorting (30 min)
   └─ TEST passes ✓

7. Integrate with EditorApp (30 min)
   └─ Manual verification

8. Setup docking layout (30 min)
   └─ Manual verification
```

---

## Estimated Time: **8-10 hours**

| Phase | Time |
|-------|------|
| Data structure | 0.5h |
| Connection lookup | 2h |
| Node rendering | 3h |
| Filtering & sorting | 2h |
| EditorApp integration | 1h |
| Docking layout | 1h |
| Testing & polish | 1h |

---

## Notes

- **No IDrawList**: Inspector uses ImGui native widgets directly
- **Const correctness**: Query methods are const, mutations via EditorApp
- **Non-owning**: Inspector holds reference to VisualScene (doesn't own data)
- **Follows existing patterns**: Same style as WireManager, BlueprintRenderer
