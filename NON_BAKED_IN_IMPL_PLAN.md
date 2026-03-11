# Non-Baked-In Sub-Blueprint: Implementation Plan

## Problem

When a non-baked-in (`baked_in=false`) sub-blueprint is added to a document, its internal nodes and wires are expanded and saved inline in the JSON file — identical to baked-in blueprints. This is architecturally wrong:

- **User edits (colors, extra nodes, positions) inside a non-baked-in sub-blueprint persist**, even though the sub-blueprint is supposed to be a read-only reference to a library definition.
- **Save is bloated**: internal nodes/wires of reference sub-blueprints duplicate what's already in `library/`.
- **Load is fragile**: if the library file changes, saved documents won't pick up the update.

### Current behavior (buggy)

```
blueprint.json → devices: [ ..., lamp_pass_through_1:vin, lamp_pass_through_1:lamp, lamp_pass_through_1:vout, and_1 (manually added!) ]
                 sub_blueprint_instances: [ { id: "lamp_pass_through_1", baked_in: false } ]
```

### Desired behavior

```
blueprint.json → devices: [ ..., lamp_pass_through_1 (collapsed node only) ]
                 sub_blueprint_instances: [ { id: "lamp_pass_through_1", baked_in: false, type_name: "lamp_pass_through", params_override: {...} } ]
                 // Internal nodes NOT saved — re-expanded from library/ on load
```

---

## Architecture Decisions

1. **Save**: `blueprint_to_editor_json()` must **skip** internal nodes and internal wires of non-baked-in sub-blueprints (identified by `group_id` matching a non-baked-in SBI).
2. **Load**: `load_editor_format()` must **re-expand** non-baked-in sub-blueprints from the TypeRegistry (using `expand_type_definition()`), then apply `params_override` and `layout_override` from the saved SBI metadata.
3. **Sub-windows**: Floating windows for non-baked-in sub-blueprints are **read-only** (no add component, no delete, no color change, no drag).
4. **"Edit Original"**: Context menu item for non-baked-in sub-blueprints opens the source library file (`library/<category>/<classname>.json`) as a new document tab.
5. **`blueprint_path`**: Must store the library-relative path (e.g., `"systems/lamp_pass_through"`) rather than just the classname, so we can resolve the file for "Edit Original".

---

## Phase 1: Save — Skip Non-Baked-In Internals

### Goal

`blueprint_to_editor_json()` must NOT save internal nodes or internal wires of non-baked-in sub-blueprints. Only save:

- The collapsed expandable node itself (e.g., `lamp_pass_through_1`)
- The `sub_blueprint_instances` metadata entry
- External wires connecting TO/FROM the collapsed node

### Failing Tests First

```
TEST(PersistNonBakedIn, Save_SkipsInternalNodes)
```

Build a Blueprint with:

- Top-level node `batt1`
- Non-baked-in sub-blueprint `lamp_1` with internal nodes `lamp_1:vin`, `lamp_1:lamp`, `lamp_1:vout`
- SubBlueprintInstance with `baked_in=false`

Save to JSON → parse JSON → assert:

- `devices` array contains `batt1` and `lamp_1` (collapsed node)
- `devices` array does NOT contain `lamp_1:vin`, `lamp_1:lamp`, `lamp_1:vout`

```
TEST(PersistNonBakedIn, Save_SkipsInternalWires)
```

Same setup + internal wires `lamp_1:vin.port→lamp_1:lamp.v_in` etc. + external wire `batt1.v_out→lamp_1.vin`.

Save to JSON → assert:

- `wires` array contains the external wire `batt1.v_out→lamp_1.vin`
- `wires` array does NOT contain any wire where both endpoints start with `lamp_1:`

```
TEST(PersistNonBakedIn, Save_BakedInStillSavesInternals)
```

Same setup but `baked_in=true`. Assert all internal nodes and wires ARE present in the JSON.

```
TEST(PersistNonBakedIn, Save_PreservesSubBlueprintInstanceMetadata)
```

Non-baked-in SBI with `params_override`, `layout_override`, `internal_routing`.
Save → parse JSON → assert all metadata fields are present in `sub_blueprint_instances` array.

### Implementation

**File: `src/editor/visual/scene/persist.cpp` — `blueprint_to_editor_json()`**

1. Before the devices loop, build a `std::set<std::string> non_baked_in_internals` from all non-baked-in SBIs' `internal_node_ids`.
2. In the devices loop: `if (non_baked_in_internals.count(n.id)) continue;`
3. In the wires loop: skip wires where BOTH `start.node_id` and `end.node_id` are in `non_baked_in_internals`.

---

## Phase 2: Load — Re-Expand Non-Baked-In From Library

### Goal

`load_editor_format()` must reconstruct internal nodes and wires of non-baked-in sub-blueprints by calling `expand_type_definition()` on the TypeRegistry definition.

### Failing Tests First

```
TEST(PersistNonBakedIn, Load_ReExpandsFromRegistry)
```

Construct a JSON string with:

- One top-level device `batt1`
- One collapsed expandable device `lamp_1` with `blueprint_path = "lamp_pass_through"` (no internal nodes in JSON)
- `sub_blueprint_instances: [{ id: "lamp_1", type_name: "lamp_pass_through", baked_in: false }]`

Load the JSON → assert:

- `bp.nodes` contains `lamp_1:vin`, `lamp_1:lamp`, `lamp_1:vout` (re-expanded from registry)
- Internal nodes have correct `group_id == "lamp_1"`
- Internal wires are present

```
TEST(PersistNonBakedIn, Load_AppliesParamsOverride)
```

JSON has `params_override: { "bat.v_nominal": "14.0" }` on a non-baked-in `simple_battery_1`.
Load → find node `simple_battery_1:bat` → assert `params["v_nominal"] == "14.0"`.

```
TEST(PersistNonBakedIn, Load_AppliesLayoutOverride)
```

JSON has `layout_override: { "lamp_1:lamp": { x: 500, y: 300 } }`.
Load → find node `lamp_1:lamp` → assert `pos == Pt(500, 300)`.

```
TEST(PersistNonBakedIn, Roundtrip_NonBakedIn)
```

Build full Blueprint with non-baked-in sub-blueprint → save → load → assert:

- Same number of top-level devices
- Same external wires
- Internal nodes re-created correctly
- SBI metadata preserved

```
TEST(PersistNonBakedIn, Roundtrip_MixedBakedAndNonBaked)
```

Blueprint with one baked-in and one non-baked-in sub-blueprint.
Save → load → assert:

- Baked-in internal nodes/wires preserved from JSON
- Non-baked-in internal nodes/wires re-expanded from registry
- Both SBI entries correct

### Implementation

**File: `src/editor/visual/scene/persist.cpp` — `load_editor_format()`**

After loading all devices and wires from JSON, and after loading `sub_blueprint_instances`:

```cpp
// Re-expand non-baked-in sub-blueprints from library
for (auto& sbi : bp.sub_blueprint_instances) {
    if (sbi.baked_in) continue;

    const auto* def = registry.get(sbi.type_name);
    if (!def) {
        spdlog::warn("[persist] Cannot re-expand '{}': type '{}' not in registry",
                     sbi.id, sbi.type_name);
        continue;
    }

    Blueprint sub_bp = expand_type_definition(*def, registry);

    // Prefix node IDs with sbi.id + ":"
    std::vector<std::string> internal_ids;
    for (auto& node : sub_bp.nodes) {
        node.id = sbi.id + ":" + node.id;
        node.name = node.id;
        node.group_id = sbi.id;
        internal_ids.push_back(node.id);

        // Apply layout_override if present
        auto it = sbi.layout_override.find(node.id);
        if (it != sbi.layout_override.end())
            node.pos = it->second;

        bp.nodes.push_back(std::move(node));
    }

    // Apply params_override: key format "local_node_id.param_name"
    for (const auto& [key, value] : sbi.params_override) {
        auto dot = key.find('.');
        if (dot == std::string::npos) continue;
        std::string local_id = sbi.id + ":" + key.substr(0, dot);
        std::string param = key.substr(dot + 1);
        if (Node* n = bp.find_node(local_id.c_str()))
            n->params[param] = value;
    }

    // Prefix wire IDs
    for (auto& wire : sub_bp.wires) {
        wire.start.node_id = sbi.id + ":" + wire.start.node_id;
        wire.end.node_id = sbi.id + ":" + wire.end.node_id;
        wire.id = sbi.id + ":" + wire.id;
        bp.wires.push_back(std::move(wire));
    }

    sbi.internal_node_ids = internal_ids;
}
bp.recompute_group_ids();
bp.rebuild_wire_index();
```

---

## Phase 3: blueprint_path — Store Library-Relative Path

### Goal

`SubBlueprintInstance.blueprint_path` and the collapsed node's `blueprint_path` must store the library-relative category path (e.g., `"systems/lamp_pass_through"`) instead of just the classname. This enables resolving the library file for "Edit Original".

### Failing Tests First

```
TEST(PersistNonBakedIn, BlueprintPath_ContainsCategory)
```

After `addBlueprint("lamp_pass_through", ...)`:

- `sbi.blueprint_path` == `"systems/lamp_pass_through"` (not just `"lamp_pass_through"`)

### Implementation

**File: `src/editor/document.cpp` — `Document::addBlueprint()`**

```cpp
// Before: sbi.blueprint_path = blueprint_name;
// After:
std::string category;
auto cat_it = registry.categories.find(blueprint_name);
if (cat_it != registry.categories.end())
    category = cat_it->second;
sbi.blueprint_path = category.empty() ? blueprint_name : (category + "/" + blueprint_name);
```

Same change in `src/editor/app.cpp`.

---

## Phase 4: Read-Only Sub-Windows for Non-Baked-In

### Goal

Floating sub-windows for non-baked-in sub-blueprints must be **read-only**:

- No component dragging
- No node deletion
- No color changes
- No adding new nodes/wires
- Toolbar buttons (Delete, Auto Layout) disabled
- Visual indicator: window title includes `[Read Only]` or similar

### Failing Tests First

```
TEST(SubBlueprintWindow, NonBakedIn_IsReadOnly)
```

Create a Blueprint with a non-baked-in SBI → open its sub-window → assert:

- `win.read_only == true` (new field on BlueprintWindow)

```
TEST(SubBlueprintWindow, BakedIn_IsNotReadOnly)
```

Same but `baked_in=true` → assert `win.read_only == false`.

### Implementation

**File: `src/editor/window_system.h` — `BlueprintWindow`**

- Add `bool read_only = false;` field.

**File: `src/editor/document.cpp` or wherever windows are opened**

- When opening a sub-window for a group_id, look up the SBI. If `!sbi->baked_in`, set `win.read_only = true`.

**File: `examples/an24_editor.cpp` — sub-blueprint window rendering**

- If `win.read_only`:
  - Disable toolbar buttons (Delete, Auto Layout)
  - Skip `process_window()` mouse interaction (or make input handler no-op)
  - Add `[Read Only]` to window title
  - Context menu: hide delete, color, and other edit items

---

## Phase 5: "Edit Original" Context Menu

### Goal

For non-baked-in sub-blueprints, add an "Edit Original" context menu item that opens the source library file as a new document tab. The user can then edit the original and save it.

### Failing Tests First

```
TEST(SubBlueprintMenu, EditOriginal_ResolvesLibraryPath)
```

Given a non-baked-in SBI with `blueprint_path = "systems/lamp_pass_through"`:

- Verify that `"library/" + sbi.blueprint_path + ".json"` resolves to an existing file in the library.

### Implementation

**File: `examples/an24_editor.cpp` — context menu section**

After the "Bake In (Embed)" menu item, within the same `!sb->baked_in` block:

```cpp
if (ImGui::MenuItem("Edit Original")) {
    std::string lib_path = "library/" + sb->blueprint_path + ".json";
    ws.openDocument(lib_path);
}
```

This opens the library file as a new document tab (or focuses it if already open).

---

## Phase Summary

| Phase     | Scope                               | Tests        | Files Modified                   |
| --------- | ----------------------------------- | ------------ | -------------------------------- |
| 1         | Save: skip non-baked-in internals   | 4 tests      | persist.cpp                      |
| 2         | Load: re-expand from library        | 5 tests      | persist.cpp                      |
| 3         | blueprint_path: store category path | 1 test       | document.cpp, app.cpp            |
| 4         | Read-only sub-windows               | 2 tests      | window_system.h, an24_editor.cpp |
| 5         | "Edit Original" menu item           | 1 test       | an24_editor.cpp                  |
| **Total** |                                     | **13 tests** |                                  |

## Execution Order

1. **Phase 1 + Phase 2** together (save/load are tightly coupled — tests verify roundtrip)
2. **Phase 3** (blueprint_path fix — needed by Phase 5)
3. **Phase 4** (read-only windows — independent of save/load)
4. **Phase 5** ("Edit Original" — depends on Phase 3 for correct path resolution)

## Key Functions Reference

| Function                       | File              | Line                      |
| ------------------------------ | ----------------- | ------------------------- |
| `blueprint_to_editor_json()`   | persist.cpp       | ~283                      |
| `load_editor_format()`         | persist.cpp       | ~464                      |
| `expand_type_definition()`     | blueprint.cpp     | ~259                      |
| `Document::addBlueprint()`     | document.cpp      | ~261                      |
| `WindowSystem::openDocument()` | window_system.cpp | ~24                       |
| `TypeRegistry::categories`     | json_parser.h     | (map: classname → subdir) |
| `TypeRegistry::get()`          | json_parser.h     | (lookup by classname)     |
