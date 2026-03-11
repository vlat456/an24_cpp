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

### 3. Hierarchical AOT Codegen

Codegen generates optimized C++ for **all** blueprints — both primitives and composites:

```
Codegen generates:
  ✓ Battery_solve_electrical()                (cpp_class: true, primitive)
  ✓ Bus_solve_electrical()                    (cpp_class: true, primitive)
  ✓ IndicatorLight_solve_electrical()         (cpp_class: true, primitive)
  ✓ lamp_pass_through_Systems::solve_step()   (cpp_class: false, composite)
  ✓ simple_battery_Systems::solve_step()      (cpp_class: false, composite)
  ✓ power_distribution_Systems::solve_step()  (cpp_class: false, composite of composites)
```

Composite codegen is **hierarchical** — a composite's generated `Systems` class contains
its sub-composites' `Systems` as direct fields, not flattened:

```
power_distribution_Systems
├── battery_bank_1: battery_bank_Systems   (sub-composite, direct field)
│     └── sb_1: simple_battery_Systems     (nested sub-composite)
│           ├── bat: Battery<AotProvider>   (primitive, direct field)
│           └── vin: BlueprintInput<AotProvider>
├── lamp_1: lamp_pass_through_Systems      (sub-composite, direct field)
│     ├── vin: BlueprintInput<AotProvider>
│     ├── lamp: IndicatorLight<AotProvider>
│     └── vout: BlueprintOutput<AotProvider>
└── bus_main: Bus<AotProvider>             (top-level primitive)
```

Benefits:

- **Cache locality**: sub-composite's devices are contiguous in struct layout
- **Full inlining**: compiler sees through composite boundaries
- **No runtime overhead**: same zero-cost abstraction as primitive AOT
- **Reusable**: same `Systems` class generated once per composite type, instantiated N times

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
SubBlueprintInstance (baked_in=false)  →  Bake In  →  SubBlueprintInstance (baked_in=true)
```

Use cases:

- Heavy customization diverging from the library original
- Archiving a snapshot independent of library changes
- One-off modifications without creating a new library entry

After bake-in, the group is still visually collapsible (same `SubBlueprintInstance`, same UI),
but `baked_in = true` means internal devices are saved inline and the source file is no longer tracked.
Overrides are merged permanently into device params. `blueprint_path` is preserved for origin info.

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

    bool baked_in = false;           // true = inline devices saved to JSON
                                     // false = expand from library file on load

    // Layout of collapsed node
    Pt pos;
    Pt size;

    // Instance-specific overrides (meaningful only when baked_in = false)
    std::map<std::string, std::string> params_override;   // "lamp.color" -> "green"
    std::map<std::string, Pt> layout_override;            // "vin" -> {100, 200}
    std::map<std::string, std::vector<Pt>> internal_routing;  // wire routing points

    // Internal node tracking (always populated at runtime)
    std::vector<std::string> internal_node_ids;  // ["lamp_1:vin", "lamp_1:lamp", ...]
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
  ├─ For each sub_blueprint_instance:
  │     │
  │     ├─ baked_in = false (reference):
  │     │     ├─ Compare params with defaults → params_override
  │     │     ├─ Compare layout with defaults → layout_override
  │     │     ├─ Extract internal routing_points
  │     │     ├─ Save as entry in sub_blueprints[] array
  │     │     └─ Skip internal devices in devices[] (expanded on load)
  │     │
  │     └─ baked_in = true (embedded):
  │           ├─ Save internal devices inline in devices[]
  │           ├─ Save entry in sub_blueprints[] with baked_in: true
  │           └─ Internal routing/layout saved with device positions
  │
  ├─ Write top-level devices (group_id == "")
  ├─ Write top-level wires (not internal to sub_blueprints)
  │
  └─ Result: compact JSON with references + optional inline baked-in groups
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

    // For codegen: primitives only
    std::vector<std::string> get_cpp_classes() const;

    // For codegen: composites only (hierarchical AOT)
    std::vector<std::string> get_composites() const;

    // Topological order for composite codegen (leaves first)
    std::vector<std::string> get_composites_topo_sorted() const;

    // Load all from library/ directory
    static TypeRegistry load(const std::string& library_dir = "library/");
};
```

---

## Codegen Integration

Codegen generates `Systems` classes for **all** blueprints in `library/` — both primitives and composites.

### Primitive (cpp_class: true) — Unchanged

Same as before: `AotProvider` with compile-time `Binding`, direct `solve_*()` calls.

### Composite (cpp_class: false) — NEW: Hierarchical AOT

For each composite blueprint, codegen generates a `Systems` class that:

1. **Recursively expands** `sub_blueprints` via `expand_sub_blueprint_references()`
2. **Generates nested `Systems`** for sub-composites (not flattened)
3. **Wires signals** between sub-composite ports and parent signals

```cpp
// generated/lamp_pass_through_systems.gen.h

class lamp_pass_through_Systems {
    // Primitive devices — direct fields with AotProvider
    BlueprintInput<AotProvider<Binding<PortNames::port, SIG_VIN_PORT>>> vin;
    IndicatorLight<AotProvider<Binding<PortNames::v_in, SIG_LAMP_V_IN>,
                               Binding<PortNames::v_out, SIG_LAMP_V_OUT>>> lamp;
    BlueprintOutput<AotProvider<Binding<PortNames::port, SIG_VOUT_PORT>>> vout;

    static constexpr uint32_t SIGNAL_COUNT = 4;

public:
    void solve_step(void* state, uint32_t step, float dt);
    void pre_load();
};
```

```cpp
// generated/power_distribution_systems.gen.h
// Composite of composites — hierarchical nesting

class power_distribution_Systems {
    // Sub-composite as nested Systems (not flattened!)
    lamp_pass_through_Systems lamp_1;
    lamp_pass_through_Systems lamp_2;
    simple_battery_Systems battery_bank_1;

    // Top-level primitives
    Bus<AotProvider<Binding<PortNames::v, SIG_BUS_V>>> bus_main;

    static constexpr uint32_t SIGNAL_COUNT = 12;

public:
    void solve_step(void* state, uint32_t step, float dt);
    void pre_load();
};
```

### Signal Wiring Between Levels

Parent `solve_step()` connects sub-composite external ports to parent signals:

```cpp
void power_distribution_Systems::step_0(void* state, float dt) {
    auto& st = *static_cast<SimulationState*>(state);
    // Wire parent → sub-composite
    st.across[lamp_1.SIG_VIN_PORT] = st.across[SIG_BUS_V];
    // Solve sub-composite
    lamp_1.solve_step(state, 0, dt);
    // Wire sub-composite → parent
    st.across[SIG_LAMP_1_VOUT] = st.across[lamp_1.SIG_VOUT_PORT];
    // Next sub-composite...
    lamp_2.solve_step(state, 0, dt);
    // Top-level primitive
    bus_main.solve_electrical(st, dt);
}
```

### Codegen Algorithm

```
for each TypeDefinition in registry:
  if cpp_class = true:
    generate_primitive_systems(td)     // existing logic
  else:
    generate_composite_systems(td)     // NEW

generate_composite_systems(td):
  1. Resolve sub_blueprints recursively
  2. For each sub-blueprint:
     - If cpp_class=true → direct AotProvider field (existing)
     - If cpp_class=false → nested Systems field (recursive)
  3. Allocate signal indices (parent scope)
  4. Generate solve_step() with signal wiring between levels
  5. Generate jump table (same 60-step LCM scheduling)
```

### Codegen Ordering

Topological sort by dependency: generate leaf composites first, then parents.
Cycle detection at codegen time (same as runtime — `expand_sub_blueprint_references()`).

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

Converts a sub-blueprint reference into a permanent inline snapshot.
Triggered via **right-click context menu** on collapsed sub-blueprint node → "Bake In".

**Algorithm:**

1. User right-clicks collapsed sub-blueprint node (e.g., `lamp_1`)
2. Context menu shows **"Bake In (Embed)"** option
3. On click:
   a. Find `SubBlueprintInstance` by node ID
   b. **Flatten overrides** — merge `params_override` into internal device params permanently
   c. **Flatten layout** — current `layout_override` positions become the devices' actual positions
   d. **Flatten routing** — `internal_routing` becomes wire `routing_points`
   e. **Set `baked_in = true`** on the `SubBlueprintInstance`
   f. **Clear overrides** — `params_override`, `layout_override`, `internal_routing` become empty
   (values already merged into actual node data)
   g. **`blueprint_path`** preserved as origin info (display only, not used for loading)
4. **On save:** internal devices serialize inline in `devices[]` array; `sub_blueprints` entry
   has `baked_in: true` flag
5. **Visual result:** no visible change — group still expandable via double-click,
   still collapsible. Only difference is the JSON representation.

**Confirmation dialog:** "This will permanently embed the sub-blueprint. Changes to the
library file will no longer affect this instance. Continue?"

**What changes after bake-in:**

| Aspect       | Before (`baked_in=false`)           | After (`baked_in=true`)                                 |
| ------------ | ----------------------------------- | ------------------------------------------------------- |
| JSON storage | `sub_blueprints[{baked_in: false}]` | `sub_blueprints[{baked_in: true}]` + inline `devices[]` |
| Library sync | Auto-updates on load                | Frozen snapshot                                         |
| Params       | Stored as overrides                 | Stored as actual values on nodes                        |
| Undo         | Reversible via undo stack           | Reversible via undo stack                               |
| Visual       | Identical                           | Identical                                               |

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

| Old                          | New                                       | Notes                                                                   |
| ---------------------------- | ----------------------------------------- | ----------------------------------------------------------------------- |
| `TypeDefinition`             | `TypeBlueprint`                           | Unified structure (NOT editor `Blueprint`)                              |
| `CollapsedGroup`             | **Removed entirely**                      | Replaced by `SubBlueprintInstance` with `baked_in` flag                 |
| `ParserContext.templates`    | Remove                                    | Everything is TypeBlueprint                                             |
| `connections` (json_parser)  | Keep as-is                                | Simulator format `"device.port"` — different from editor `Wire` structs |
| Inline sub-blueprint devices | References by default                     | `sub_blueprints[]` array, `baked_in: true` for inline                   |
| `collapsed_groups` vector    | `sub_blueprint_instances` with `baked_in` | Single vector handles both modes                                        |

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

### Phase 5: Hierarchical AOT Codegen

- Extend `src/codegen/codegen.cpp` to handle `cpp_class: false` composites
- Recursive `generate_composite_systems()` — nested `Systems` fields for sub-composites
- Signal wiring between parent and sub-composite ports
- Topological sort for codegen ordering (leaves first)
- Jump table scheduling same as primitives (60-step LCM)
- Tests: generated composite `Systems` produces same results as JIT

### Phase 6: Cleanup

- Migrate `TypeDefinition` → `TypeBlueprint` (or keep name and extend with `sub_blueprints`)
- **Remove `CollapsedGroup` entirely** — `SubBlueprintInstance` with `baked_in` flag replaces it
- Remove `ParserContext.templates`
- Update tests

### Phase 7: Testing

- Cycle detection tests
- Hierarchical loading tests
- Override tests
- Save/load roundtrip tests
- **Bake-in tests**: reference → baked-in conversion (`baked_in` flag flip), params flattening, save/load roundtrip
- **Mixed mode tests**: files with both `baked_in=false` and `baked_in=true` SubBlueprintInstances
- **AOT composite tests**: generated `Systems` for composites matches JIT output
- **Hierarchical AOT tests**: 2-3 level nesting, signal wiring correctness
- **Benchmark**: AOT composite vs JIT composite performance comparison

---

## File Changes Summary

| File                                  | Changes                                                                                |
| ------------------------------------- | -------------------------------------------------------------------------------------- |
| `src/json_parser/json_parser.h`       | Extend `TypeDefinition` with `sub_blueprints`, add `SubBlueprintInstance`              |
| `src/json_parser/json_parser.cpp`     | Recursive loading, cycle detection, override application                               |
| `src/editor/data/blueprint.h`         | **Remove `CollapsedGroup`**, add `SubBlueprintInstance` with `baked_in`, single vector |
| `src/editor/visual/scene/persist.cpp` | New save/load format (`baked_in` flag controls inline vs reference)                    |
| `src/editor/document.cpp`             | `addBlueprint()`, `bakeInSubBlueprint()`, override tracking                            |
| `src/editor/window_system.cpp`        | Context menu integration for bake-in                                                   |
| `examples/an24_editor.cpp`            | Context menu rendering (bake-in option)                                                |
| `src/codegen/codegen.h`               | New `generate_composite_systems()` API                                                 |
| `src/codegen/codegen.cpp`             | Hierarchical AOT: recursive composite codegen, signal wiring                           |
| `generated/*.gen.h/cpp`               | Generated `Systems` classes for composites                                             |
| `tests/*.cpp`                         | Update for new format, bake-in roundtrip, AOT composite tests                          |
| `library/**/*.json`                   | Update format (no inline expansion)                                                    |

---

## Risks & Mitigations

| Risk                               | Mitigation                                                    |
| ---------------------------------- | ------------------------------------------------------------- |
| Complex override logic             | Clear precedence rules, extensive tests                       |
| Editor state sync                  | Single source of truth in `Blueprint`                         |
| Undo/redo with references          | Store operations as diffs, apply to overrides                 |
| Performance (recursive load)       | Cache loaded blueprints in TypeRegistry                       |
| Missing files                      | Fail fast with clear error message                            |
| Bake-in data loss                  | Confirmation dialog, undo support                             |
| Naming collision (`Blueprint`)     | Use `TypeBlueprint` for registry, keep `Blueprint` for editor |
| AOT codegen ordering               | Topological sort by dependency; fail on cycles                |
| AOT signal index explosion         | Per-composite local signals; parent allocates connection bus  |
| AOT compile time (many composites) | Incremental codegen — only regenerate changed composites      |

---

## Open Questions

1. ~~**TypeDefinition rename**~~ — **Decided:** extend existing `TypeDefinition` with `sub_blueprints` field. Minimal diff.
2. **Bake-in undo granularity**: Should bake-in be a single undo operation or decomposed into sub-steps?
3. ~~**Mixed mode per-file**~~ — **Decided:** yes. A single blueprint JSON can have `SubBlueprintInstance` entries with different `baked_in` flag values. `baked_in=false` = reference, `baked_in=true` = embedded. Single vector, single struct, one flag.
4. ~~**Node::blueprint_path field**~~ — **Decided:** reuse. Already exists on `Node`, already serialized by persist.cpp. Set to `type_name` on the collapsed wrapper node.
5. ~~**load_editor_format() signature**~~ — **Decided:** single signature with mandatory `TypeRegistry` parameter. All callers provide a registry. Tests create a minimal registry. No overload.
