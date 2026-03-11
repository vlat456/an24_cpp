# Sub-Blueprint References: Architecture & Implementation Plan

## Problem Statement

Currently, sub-blueprint instances are stored inline in `devices[]` array with prefixed names (e.g., `lamp_1:vin`, `lamp_1:lamp`). This creates duplication - if `library/systems/lamp_pass_through.json` changes, all parent blueprints must be manually updated.

**Goal**: Store references to sub-blueprint files, expand on load. Runtime (JIT/AOT) sees flat list as before.

---

## Core Principles

### 1. Everything is Blueprint

All JSON files use the same `Blueprint` structure:

```
library/electrical/Battery.json    → Blueprint (cpp_class: true)
library/systems/lamp_pass_through  → Blueprint (cpp_class: false)
blueprint.json (user's work)       → Blueprint (cpp_class: false)
```

No distinction between "type definition" and "blueprint" at file level.

### 2. Universal Container

```
Blueprint
├── cpp_class = true  → C++ implementation (primitive, not expanded)
└── cpp_class = false → Composite (devices/wires/sub_blueprints)
```

Simulator sees only: `devices + wires` (flat list after expansion).

### 3. Codegen Independence

Adding a new blueprint to `library/` does **NOT** require codegen.

```
Codegen generates:
  ✓ Battery_solve_electrical()     (cpp_class: true)
  ✓ Bus_solve_electrical()         (cpp_class: true)
  ✓ IndicatorLight_solve_electrical() (cpp_class: true)

Codegen does NOT generate:
  ✗ lamp_pass_through              (composite of primitives)
  ✗ simple_battery                 (composite of primitives)
  ✗ user_circuit                   (composite)
```

### 4. Hierarchical References

Sub-blueprints can be nested arbitrarily deep:

```
power_distribution.json
  └── sub: battery_bank.json
        └── sub: simple_battery.json
              └── devices: [bat, vin, vout]
```

Result after expansion: `battery_bank_1:simple_battery_1:bat` in flat list.

### 5. Bake-In Escape Hatch

Any sub-blueprint reference can be permanently embedded ("baked in") via context menu.
This severs the link to the library file — internal devices become part of the parent JSON.

```
sub_blueprint (reference)  →  Bake In  →  inline devices + collapsed_group
```

Use cases:

- Heavy customization diverging from the library original
- Archiving a snapshot independent of library changes
- One-off modifications without creating a new library entry

After bake-in, the group is still visually collapsible (CollapsedGroup metadata preserved),
but no longer tracks the source file. Overrides are merged permanently into device params.

---

## Data Structures

> **Naming note:** The editor already has `struct Blueprint` in `src/editor/data/blueprint.h`
> (nodes, wires, pan, zoom — the visual working document). To avoid collision, the universal
> registry type below is named `TypeBlueprint`. Alternatively, keep `TypeDefinition` and
> extend it with `sub_blueprints`. The editor `Blueprint` stays unchanged.

### TypeBlueprint (Universal Registry Type)

```cpp
// Single structure for ALL JSON files in library/ and TypeRegistry
// NOT the same as editor::Blueprint (nodes, wires, viewport)
struct TypeBlueprint {
    // Identity
    std::string classname;           // "Battery", "lamp_pass_through", "my_circuit"
    std::string description;
    bool cpp_class = false;          // true = C++ impl, false = composite

    // Type definition (for both cpp_class and composite)
    std::unordered_map<std::string, Port> ports;
    std::unordered_map<std::string, std::string> params;
    std::vector<Domain> domains;
    std::string priority = "med";
    bool critical = false;

    // Content type (for UI)
    std::string content_type = "None";
    std::string render_hint;         // "bus", "ref", or empty
    bool visual_only = false;
    std::optional<std::pair<float, float>> size;

    // Composite only (cpp_class = false)
    std::vector<SubBlueprintInstance> sub_blueprints;
    std::vector<DeviceInstance> devices;
    std::vector<Connection> connections;  // with optional routing_points

    // Editor metadata (optional for library files)
    std::optional<ViewportState> viewport;
};
```

### SubBlueprintInstance

```cpp
struct SubBlueprintInstance {
    std::string id;                  // Unique instance ID: "lamp_1"
    std::string blueprint_path;      // "library/systems/lamp_pass_through.json"
    std::string type_name;           // "lamp_pass_through" (for UI display)

    // Layout of collapsed node
    Pt pos;
    Pt size;

    // Instance-specific overrides
    std::map<std::string, std::string> params_override;   // "lamp.color" -> "green"
    std::map<std::string, Pt> layout_override;            // "vin" -> {100, 200}
    std::map<std::string, std::vector<Pt>> internal_routing;  // wire routing points
};
```

### DeviceInstance (unchanged)

```cpp
struct DeviceInstance {
    std::string name;
    std::string classname;
    std::unordered_map<std::string, Port> ports;
    std::unordered_map<std::string, std::string> params;
    std::vector<Domain> domains;
    std::optional<std::pair<float, float>> pos;
    std::optional<std::pair<float, float>> size;
    // ...
};
```

---

## JSON Format

### Primitive (C++ implementation)

```json
// library/electrical/Battery.json
{
  "classname": "Battery",
  "description": "DC voltage source with internal resistance",
  "cpp_class": true,
  "ports": {
    "v_in": { "direction": "In", "type": "V" },
    "v_out": { "direction": "Out", "type": "V" }
  },
  "params": {
    "v_nominal": "28.0",
    "internal_r": "0.01",
    "capacity": "1000.0"
  },
  "domains": ["Electrical"],
  "priority": "high",
  "critical": true
}
```

### Composite Blueprint (library)

```json
// library/systems/lamp_pass_through.json
{
  "classname": "lamp_pass_through",
  "description": "Voltage pass-through with indicator lamp",
  "cpp_class": false,
  "ports": {
    "vin": { "direction": "In", "type": "V" },
    "vout": { "direction": "Out", "type": "V" }
  },
  "domains": ["Electrical"],
  "devices": [
    {
      "name": "vin",
      "classname": "BlueprintInput",
      "params": { "exposed_type": "V", "exposed_direction": "In" }
    },
    {
      "name": "lamp",
      "classname": "IndicatorLight",
      "params": { "color": "red", "max_brightness": "100.0" }
    },
    {
      "name": "vout",
      "classname": "BlueprintOutput",
      "params": { "exposed_type": "V", "exposed_direction": "Out" }
    }
  ],
  "wires": [
    { "from": "vin.port", "to": "lamp.v_in" },
    { "from": "lamp.v_out", "to": "vout.port" }
  ]
}
```

### User Blueprint (with sub-blueprint references)

```json
// blueprint.json (user's work)
{
  "classname": "my_circuit",
  "cpp_class": false,
  "domains": ["Electrical"],

  "sub_blueprints": [
    {
      "id": "lamp_1",
      "blueprint_path": "library/systems/lamp_pass_through.json",
      "type_name": "lamp_pass_through",
      "pos": { "x": 400, "y": 300 },
      "size": { "x": 120, "y": 80 },
      "params_override": {
        "lamp.color": "green"
      },
      "layout_override": {
        "vin": { "x": 350, "y": 300 },
        "lamp": { "x": 400, "y": 300 },
        "vout": { "x": 500, "y": 300 }
      },
      "internal_routing": {
        "vin.port->lamp.v_in": [{ "x": 375, "y": 310 }],
        "lamp.v_out->vout.port": [{ "x": 475, "y": 310 }]
      }
    },
    {
      "id": "lamp_2",
      "blueprint_path": "library/systems/lamp_pass_through.json",
      "type_name": "lamp_pass_through",
      "pos": { "x": 400, "y": 500 },
      "size": { "x": 120, "y": 80 }
      // No overrides = use defaults from file
    }
  ],

  "devices": [
    {
      "name": "bat_main",
      "classname": "Battery",
      "params": { "v_nominal": "28.0" },
      "pos": { "x": 100, "y": 300 }
    },
    {
      "name": "bus_main",
      "classname": "Bus",
      "pos": { "x": 200, "y": 300 }
    }
  ],

  "wires": [
    { "from": "bat_main.v_out", "to": "bus_main.v" },
    { "from": "bus_main.v", "to": "lamp_1.vin" },
    { "from": "lamp_1.vout", "to": "lamp_2.vin" }
  ],

  "viewport": {
    "pan": { "x": 0, "y": 0 },
    "zoom": 1.0,
    "grid_step": 16.0
  }
}
```

---

## Loading Flow

```
load_blueprint("blueprint.json")
  │
  ├─ Parse JSON → Blueprint
  │
  ├─ For each sub_blueprint:
  │     │
  │     ├─ load_blueprint(sub.blueprint_path)
  │     │     │
  │     │     └─ Recursive: may have its own sub_blueprints
  │     │
  │     ├─ Prefix device names: "lamp" → "lamp_1:lamp"
  │     ├─ Apply params_override
  │     ├─ Apply layout_override
  │     └─ Add to flat devices/connections
  │
  ├─ Add top-level devices (no prefix)
  ├─ Add top-level wires
  │
  └─ Result: ParserContext (flat list, ready for JIT/codegen)
```

### Cycle Detection

```cpp
std::set<std::string> loading_paths;  // Track currently loading files

void load_blueprint(path) {
    if (loading_paths.contains(path)) {
        throw "Circular reference: " + path;
    }
    loading_paths.insert(path);
    // ... load ...
    loading_paths.erase(path);
}
```

### Error Handling

If `blueprint_path` not found → **fail load with error** (no fallback, no placeholder).

---

## Saving Flow

```
save_blueprint(Blueprint, path)
  │
  ├─ Separate nodes by type:
  │     ├─ Nodes with SubBlueprintInstance → save as sub_blueprints references
  │     └─ Nodes with CollapsedGroup (baked-in) → save inline in devices[]
  │
  ├─ For each sub_blueprint (reference):
  │     │
  │     ├─ Find source file (from TypeRegistry by type_name)
  │     ├─ Compare params with defaults → params_override
  │     ├─ Compare layout with defaults → layout_override
  │     └─ Extract internal routing_points
  │
  ├─ Write sub_blueprints array
  ├─ Write top-level devices only (group_id == "")
  ├─ Write top-level wires only (not internal to sub_blueprints)
  │
  └─ Result: compact JSON with references
```

---

## TypeRegistry Changes

### Before

```cpp
struct TypeRegistry {
    std::unordered_map<std::string, TypeDefinition> types;
};
```

### After

```cpp
struct TypeRegistry {
    std::unordered_map<std::string, TypeBlueprint> types;

    // Get by classname (works for both cpp_class and composite)
    const TypeBlueprint* get(const std::string& classname) const;

    // For codegen: filter cpp_class = true only
    std::vector<std::string> get_cpp_classes() const;

    // Load all from library/ directory
    static TypeRegistry load(const std::string& library_dir = "library/");
};
```

---

## Codegen Integration

**No changes required for new blueprints.**

Codegen scans `library/`, filters `cpp_class: true`, generates C++:

```cpp
// generated/components.gen.cpp

void solve_electrical_all(SimulationState& st, float dt) {
    // Only cpp_class = true primitives
    for (auto& dev : st.devices) {
        if (dev.classname == "Battery") Battery_solve(dev, st, dt);
        else if (dev.classname == "Bus") Bus_solve(dev, st, dt);
        else if (dev.classname == "IndicatorLight") IndicatorLight_solve(dev, st, dt);
        // ... other primitives ...
    }
}
```

Composite blueprints (`lamp_pass_through`, `simple_battery`) are expanded at load time, their primitives are already in codegen.

---

## Editor Integration

### Adding Sub-Blueprint

1. User drags blueprint from menu to canvas
2. Create `SubBlueprintInstance` with id, path, pos, size
3. Load blueprint file to get exposed ports
4. Display collapsed node with exposed ports

### Expanding Sub-Blueprint

1. User double-clicks collapsed node
2. Load internal devices/wires from blueprint file
3. Apply `layout_override` (or auto-layout if none)
4. Show internal nodes in sub-window or inline

### Collapsing Sub-Blueprint

1. User clicks collapse button
2. Collect current positions → `layout_override`
3. Collect internal routing_points → `internal_routing`
4. Hide internal nodes, show collapsed representation

### Editing Parameters

1. User edits parameter on internal device
2. Compare with default from source file
3. If different → store in `params_override`
4. UI shows override indicator

### Bake In (Embed Permanently) — Context Menu

Converts a sub-blueprint reference into permanent inline devices (current format).
Triggered via **right-click context menu** on collapsed sub-blueprint node → "Bake In".

**Algorithm:**

1. User right-clicks collapsed sub-blueprint node (e.g., `lamp_1`)
2. Context menu shows **"Bake In (Embed)"** option
3. On click:
   a. Find `SubBlueprintInstance` by node ID
   b. **Flatten overrides** — merge `params_override` into internal device params permanently
   c. **Flatten layout** — current `layout_override` positions become the devices' actual positions
   d. **Flatten routing** — `internal_routing` becomes wire `routing_points`
   e. **Convert to CollapsedGroup** — create `CollapsedGroup` entry with same `id`, `type_name`,
   `pos`, `size`, `internal_node_ids` (all prefixed internal nodes)
   f. **Remove SubBlueprintInstance** entry from `sub_blueprints[]`
   g. **Mark internal nodes** — set `group_id` on all internal devices (already present)
   h. **Clear `blueprint_path`** reference on collapsed wrapper node
4. **On save:** internal devices serialize inline in `devices[]` array (not as reference)
5. **Visual result:** no visible change — group still expandable via double-click,
   still collapsible. Only difference is the JSON representation.

**Confirmation dialog:** "This will permanently embed the sub-blueprint. Changes to the
library file will no longer affect this instance. Continue?"

**What changes after bake-in:**

| Aspect       | Before (reference)           | After (baked in)                     |
| ------------ | ---------------------------- | ------------------------------------ |
| JSON storage | `sub_blueprints[{id, path}]` | `devices[{name: "lamp_1:vin", ...}]` |
| Library sync | Auto-updates on load         | Frozen snapshot                      |
| Params       | Stored as overrides          | Stored as actual values              |
| Undo         | Reversible via undo stack    | Reversible via undo stack            |
| Visual       | Identical                    | Identical                            |

---

## File Structure (Unchanged)

```
library/
├── electrical/
│   ├── Battery.json          (cpp_class: true)
│   ├── Bus.json              (cpp_class: true)
│   ├── IndicatorLight.json   (cpp_class: true)
│   └── ...
├── systems/
│   ├── lamp_pass_through.json (cpp_class: false)
│   ├── simple_battery.json    (cpp_class: false)
│   └── ...
├── BlueprintInput.json        (cpp_class: true)
├── BlueprintOutput.json       (cpp_class: true)
└── blueprint.json             (cpp_class: false)

User files (anywhere):
├── blueprint.json             (cpp_class: false, sub_blueprints: [...])
├── my_circuit.json            (cpp_class: false, sub_blueprints: [...])
└── ...
```

---

## Migration / Backward Compatibility

**No backward compatibility required.** This is a clean redesign.

Migration path:

1. Delete old `blueprint.json` files
2. Re-create with new format
3. Or write one-time migration script (not recommended)

---

## Deprecated / Removed

| Old                          | New                    | Notes                                                                   |
| ---------------------------- | ---------------------- | ----------------------------------------------------------------------- |
| `TypeDefinition`             | `TypeBlueprint`        | Unified structure (NOT editor `Blueprint`)                              |
| `CollapsedGroup`             | `SubBlueprintInstance` | With overrides (CollapsedGroup kept for baked-in groups)                |
| `ParserContext.templates`    | Remove                 | Everything is TypeBlueprint                                             |
| `connections` (json_parser)  | Keep as-is             | Simulator format `"device.port"` — different from editor `Wire` structs |
| Inline sub-blueprint devices | References by default  | `sub_blueprints[]` array, with "Bake In" to convert back to inline      |

---

## Implementation Phases

### Phase 1: Data Structures

- Define `TypeBlueprint` struct (unified registry type, extends current `TypeDefinition`)
- Define `SubBlueprintInstance`
- Update `TypeRegistry` to store `TypeBlueprint`
- Keep editor `Blueprint` (nodes/wires/viewport) unchanged

### Phase 2: JSON Parser

- Parse `sub_blueprints` array
- Recursive loading with cycle detection
- Apply overrides (params, layout, routing)
- Generate flat `ParserContext`

### Phase 3: Persistence

- New save format (references, not inline)
- New load format (expand references)
- Viewport metadata

### Phase 4: Editor Integration

- Add sub-blueprint to canvas (via `Document::addBlueprint()`)
- Expand/collapse UI (double-click → `Document::openSubWindow()`)
- Override tracking in Inspector
- Parameter editing with diff-to-default
- **Bake-in context menu** — right-click → "Bake In (Embed)" → convert reference to inline
- Confirmation dialog for bake-in operation

### Phase 5: Cleanup

- Migrate `TypeDefinition` → `TypeBlueprint` (or keep name and extend with `sub_blueprints`)
- Keep `CollapsedGroup` for baked-in groups, add `SubBlueprintInstance` for references
- Remove `ParserContext.templates`
- Update tests

### Phase 6: Testing

- Cycle detection tests
- Hierarchical loading tests
- Override tests
- Save/load roundtrip tests
- **Bake-in tests**: reference → baked-in conversion, params flattening, save/load roundtrip
- **Mixed mode tests**: files with both `sub_blueprints` and baked-in `collapsed_groups`

---

## File Changes Summary

| File                                  | Changes                                                                   |
| ------------------------------------- | ------------------------------------------------------------------------- |
| `src/json_parser/json_parser.h`       | Extend `TypeDefinition` with `sub_blueprints`, add `SubBlueprintInstance` |
| `src/json_parser/json_parser.cpp`     | Recursive loading, cycle detection, override application                  |
| `src/editor/data/blueprint.h`         | Add `SubBlueprintInstance`, keep `CollapsedGroup` for baked-in            |
| `src/editor/visual/scene/persist.cpp` | New save/load format (references + baked-in inline)                       |
| `src/editor/document.cpp`             | `addBlueprint()`, `bakeInSubBlueprint()`, override tracking               |
| `src/editor/window_system.cpp`        | Context menu integration for bake-in                                      |
| `examples/an24_editor.cpp`            | Context menu rendering (bake-in option)                                   |
| `tests/*.cpp`                         | Update for new format, bake-in roundtrip tests                            |
| `library/**/*.json`                   | Update format (no inline expansion)                                       |

---

## Risks & Mitigations

| Risk                           | Mitigation                                                    |
| ------------------------------ | ------------------------------------------------------------- |
| Complex override logic         | Clear precedence rules, extensive tests                       |
| Editor state sync              | Single source of truth in `Blueprint`                         |
| Undo/redo with references      | Store operations as diffs, apply to overrides                 |
| Performance (recursive load)   | Cache loaded blueprints in TypeRegistry                       |
| Missing files                  | Fail fast with clear error message                            |
| Bake-in data loss              | Confirmation dialog, undo support                             |
| Naming collision (`Blueprint`) | Use `TypeBlueprint` for registry, keep `Blueprint` for editor |

---

## Open Questions

1. **TypeDefinition rename**: Extend existing `TypeDefinition` with `sub_blueprints` field (minimal change) vs create new `TypeBlueprint` struct (clean break)?
2. **Bake-in undo granularity**: Should bake-in be a single undo operation or decomposed into sub-steps?
3. **Mixed mode per-file**: Can a single blueprint JSON contain both `sub_blueprints` references AND baked-in `collapsed_groups`? (Proposed: yes — this is the natural result of selective bake-in.)
4. **Node::blueprint_path field**: Already exists on `Node` and is serialized by persist.cpp. Should SubBlueprintInstance reuse this field or use its own?
