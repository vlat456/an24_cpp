# JSON Format v2: Implementation Plan

## Overview

Replace all three JSON formats (library type definitions, simulation blueprint, editor save) with a single unified v2 schema where **everything is a Blueprint**. No backward compatibility — clean break, all legacy deleted.

**Design doc**: `JSON_FORMAT_V2_DESIGN.md`  
**Approach**: Failing-first TDD — write tests, then implement  
**File extension**: `.blueprint` for all blueprint files

---

## Architecture Strategy

### Key Insight: Two Separate Concerns

The v2 migration affects two independent subsystems:

1. **Library loading** — `parse_type_definition()` reads `library/**/*.json` into `TypeRegistry`. After v2: reads `library/**/*.blueprint` using the unified schema.

2. **Editor persistence** — `blueprint_to_editor_json()` / `load_editor_format()` save/load editor documents. After v2: uses `serialize_blueprint_v2()` / `parse_blueprint_v2()`.

These share the same v2 JSON schema but have different code paths. The library loader produces `TypeDefinition` structs; the editor serializer works with `Blueprint` + `Node` + `Wire` structs.

### The Blueprint→JSON→parse→build Roundtrip

`SimulationController::build()` currently does:
```
Blueprint → blueprint_to_json() → JSON string → parse_json() → ParserContext → build_systems_dev()
```

This intermediate JSON serialization exists only because `build_systems_dev()` requires `vector<DeviceInstance>` + `vector<Connection>`. Rather than maintaining a v2 simulation serializer, **Phase 4 introduces a direct `Blueprint → BuildInput` path** that skips JSON entirely.

### What Gets Deleted

| Code | Location | Reason |
|------|----------|--------|
| `blueprint_to_json()` | persist.cpp:83 | Eliminated by direct build path |
| `blueprint_to_editor_json()` | persist.cpp:~283 | Replaced by `serialize_blueprint_v2()` |
| `load_editor_format()` | persist.cpp:~464 | Replaced by `parse_blueprint_v2()` |
| `blueprint_from_json()` | persist.cpp | Replaced by `parse_blueprint_v2()` |
| `baked_in` flag | blueprint.h:21 | Presence of `nodes` key = embedded mode |
| `collapsed_groups` parsing | persist.cpp | Dead format field |
| All dedup code (save+load) | persist.cpp | Object keys are unique |
| `group_id` string prefixing | persist.cpp, document.cpp | Scope is structural |
| `connections` array in library | json_parser.cpp | Unified as `wires` |
| `parse_type_definition()` v1 | json_parser.cpp | Replaced by v2 loader |
| `serialize_json()` / `parse_json()` sim format | json_parser.cpp | Eliminated by direct build |

---

## Phase 1: v2 Data Structures + Parser/Serializer

### Goal

Implement the core v2 C++ types and a standalone parse/serialize pair that can roundtrip v2 JSON. No integration with existing code yet — pure unit tests.

### New Files

- `src/v2/blueprint_v2.h` — v2 data structures
- `src/v2/blueprint_v2.cpp` — `parse_blueprint_v2()` + `serialize_blueprint_v2()`
- `tests/test_blueprint_v2.cpp` — all Phase 1 tests

### v2 Data Structures

```cpp
// src/v2/blueprint_v2.h
#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>

namespace an24::v2 {

struct ExposedPort {
    std::string direction;  // "In", "Out", "InOut"
    std::string type;       // "V", "I", "Bool", "RPM", "Temperature", "Pressure", "Position", "Any"
};

struct ParamDef {
    std::string type;       // "float", "int", "bool", "string"
    std::string default_val;
};

struct ContentV2 {
    std::string kind;       // "gauge", "switch", "vertical_toggle", "hold_button", "text"
    std::string label;
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    std::string unit;
    bool state = false;
};

struct NodeColorV2 {
    float r = 0.5f, g = 0.5f, b = 0.5f, a = 1.0f;
};

struct NodeV2 {
    std::string type;                                   // Component type ("Battery", "Bus")
    std::array<float, 2> pos = {0.0f, 0.0f};
    std::array<float, 2> size = {0.0f, 0.0f};          // {0,0} = use default
    std::map<std::string, std::string> params;
    std::optional<ContentV2> content;
    std::optional<NodeColorV2> color;
};

struct WireEndV2 {
    std::string node;
    std::string port;
};

struct WireV2 {
    std::string id;
    WireEndV2 from;
    WireEndV2 to;
    std::vector<std::array<float, 2>> routing;
};

struct OverridesV2 {
    std::map<std::string, std::string> params;                    // "node.param" → value
    std::map<std::string, std::array<float, 2>> layout;           // node_id → [x,y]
    std::map<std::string, std::vector<std::array<float, 2>>> routing;  // wire_id → points
};

struct SubBlueprintV2 {
    // Reference mode
    std::optional<std::string> template_path;   // "library/systems/lamp_pass_through"

    // Common visual
    std::array<float, 2> pos = {0.0f, 0.0f};
    std::array<float, 2> size = {0.0f, 0.0f};
    bool collapsed = true;

    // Overrides (reference mode only)
    std::optional<OverridesV2> overrides;

    // Embedded mode (presence of nodes → embedded)
    std::map<std::string, NodeV2> nodes;
    std::vector<WireV2> wires;
};

struct MetaV2 {
    std::string name;
    std::string description;
    std::vector<std::string> domains;
    bool cpp_class = false;
};

struct ViewportV2 {
    std::array<float, 2> pan = {0.0f, 0.0f};
    float zoom = 1.0f;
    float grid = 16.0f;
};

struct BlueprintV2 {
    int version = 2;
    MetaV2 meta;

    std::map<std::string, ExposedPort> exposes;
    std::map<std::string, ParamDef> params;     // cpp_class=true only

    std::map<std::string, NodeV2> nodes;
    std::vector<WireV2> wires;
    std::map<std::string, SubBlueprintV2> sub_blueprints;

    std::optional<ViewportV2> viewport;         // Root documents only
};

// Parse v2 JSON string → BlueprintV2
std::optional<BlueprintV2> parse_blueprint_v2(const std::string& json_text);

// Serialize BlueprintV2 → pretty JSON string
std::string serialize_blueprint_v2(const BlueprintV2& bp);

} // namespace an24::v2
```

### Failing Tests First (12 tests)

```
TEST(BlueprintV2Parse, MinimalCppComponent)
```
Parse a C++ component (`cpp_class: true`, `exposes`, `params`, no `nodes`). Assert all fields.

```
TEST(BlueprintV2Parse, CompositeBlueprint)
```
Parse a composite blueprint (`cpp_class: false`, `exposes`, `nodes`, `wires`). Assert node map, wire endpoints.

```
TEST(BlueprintV2Parse, RootDocument)
```
Parse a full root document with `viewport`, `nodes`, `wires`, `sub_blueprints`. Assert viewport, sub-blueprint fields.

```
TEST(BlueprintV2Parse, SubBlueprintReference)
```
Parse a sub-blueprint in reference mode (`template` + `overrides`). Assert `template_path`, `overrides.params`, `overrides.layout`.

```
TEST(BlueprintV2Parse, SubBlueprintEmbedded)
```
Parse a sub-blueprint in embedded mode (`nodes` + `wires`, optional `template` for provenance). Assert inline nodes exist, `template_path` preserved.

```
TEST(BlueprintV2Parse, WireEndpointsAsArrays)
```
Parse wires with `["node", "port"]` endpoints. Assert `from.node`, `from.port`, `to.node`, `to.port`.

```
TEST(BlueprintV2Parse, PositionsAsArrays)
```
Parse nodes with `"pos": [96, 112]`. Assert `pos[0] == 96`, `pos[1] == 112`.

```
TEST(BlueprintV2Parse, NodeContent)
```
Parse a node with `content` object. Assert `kind`, `label`, `unit`, `min`, `max`.

```
TEST(BlueprintV2Parse, NodeColor)
```
Parse a node with `color` object. Assert RGBA values.

```
TEST(BlueprintV2Parse, InvalidVersionReturnsNullopt)
```
Parse JSON with `"version": 1`. Assert returns `std::nullopt`.

```
TEST(BlueprintV2Serialize, Roundtrip)
```
Construct a `BlueprintV2` programmatically → serialize → parse → assert all fields match.

```
TEST(BlueprintV2Serialize, RoundtripWithSubBlueprints)
```
Same but with both reference and embedded sub-blueprints.

### Implementation

**`src/v2/blueprint_v2.cpp`**: ~300 lines

- `parse_blueprint_v2()`: Parse `version`, `meta`, `exposes`, `params`, `nodes` (object → map), `wires`, `sub_blueprints`, `viewport`. Return `nullopt` on parse error or wrong version.
- `serialize_blueprint_v2()`: Reverse of parse. `$schema` and `version` at top. Skip empty optional sections. Pretty print with 2-space indent.

---

## Phase 2: Library Conversion (`.json` → `.blueprint`)

### Goal

1. Write a conversion function `TypeDefinition → BlueprintV2` (C++ code, not a Python script).
2. Convert all 70 library files from v1 `.json` to v2 `.blueprint`.
3. Update `load_type_registry()` to read `.blueprint` files using `parse_blueprint_v2()`.
4. Update `parse_type_definition()` to produce `TypeDefinition` from `BlueprintV2`.

### New/Modified Files

- `src/v2/convert.h` / `src/v2/convert.cpp` — conversion functions between v1 and v2 types
- `src/json_parser/json_parser.cpp` — update `load_type_registry()` + `parse_type_definition()`
- `tests/test_library_v2.cpp` — library loading tests
- `examples/convert_library.cpp` — one-shot converter tool

### Critical: Port Alias Preservation

Port `alias` is load-bearing for signal merging in JIT/AOT. v2 `exposes` ports can have `alias`:

```json
"exposes": {
    "v_out": {"direction": "Out", "type": "V", "alias": "v_in"}
}
```

The conversion must preserve alias from `TypeDefinition.ports[name].alias` → `ExposedPort.alias` (add `std::optional<std::string> alias` to `ExposedPort`).

### Failing Tests First (8 tests)

```
TEST(LibraryV2, ConvertCppComponent)
```
Convert a `TypeDefinition` (Battery: `cpp_class=true`, 4 ports, 2 params) → `BlueprintV2`. Assert `meta.cpp_class == true`, `exposes` has 4 entries, `params` has 2 entries, no `nodes`.

```
TEST(LibraryV2, ConvertComposite)
```
Convert a composite `TypeDefinition` (lamp_pass_through: devices + connections) → `BlueprintV2`. Assert `nodes` populated, `wires` populated, `exposes` correct.

```
TEST(LibraryV2, PortAliasPreserved)
```
Convert a component with aliased ports → `BlueprintV2`. Assert `exposes["v_out"].alias == "v_in"`.

```
TEST(LibraryV2, LoadRegistryFromBlueprint)
```
Create a temp directory with `.blueprint` files → `load_type_registry(temp_dir)` → assert registry has correct types.

```
TEST(LibraryV2, LoadRegistryPreservesCategories)
```
Load registry → check `categories["Battery"] == "electrical"` etc.

```
TEST(LibraryV2, LoadRegistryCompositesHaveDevices)
```
Load registry → check that composite `TypeDefinition` has `devices` and `connections` populated (back-converted from v2 `nodes`/`wires`).

```
TEST(LibraryV2, RoundtripRegistryThroughV2)
```
Load registry from v1 → convert all → serialize to disk → load from v2 → compare registries.

```
TEST(LibraryV2, BlueprintExtensionOnly)
```
Place a `.json` file in library dir → `load_type_registry()` should NOT load it (only `.blueprint`).

### Implementation

1. **`src/v2/convert.cpp`** (~200 lines):
   - `BlueprintV2 type_definition_to_v2(const TypeDefinition& td, const TypeRegistry& reg)` — converts TypeDefinition to BlueprintV2
   - `TypeDefinition v2_to_type_definition(const BlueprintV2& bp)` — converts back for existing consumers

2. **`examples/convert_library.cpp`** (~50 lines): One-shot tool:
   - Load v1 registry from `library/`
   - Convert each type → `BlueprintV2` → `serialize_blueprint_v2()`
   - Write to `library/<category>/<classname>.blueprint`
   - Delete old `.json` files

3. **`src/json_parser/json_parser.cpp`** — modify `load_type_registry()`:
   - Scan for `*.blueprint` instead of `*.json`
   - Parse each file with `parse_blueprint_v2()`
   - Convert to `TypeDefinition` via `v2_to_type_definition()`
   - Rest of registry logic unchanged

### Library File Count by Category

| Category | Count | Examples |
|----------|-------|---------|
| electrical | ~15 | Battery, Bus, Switch, Voltmeter |
| mechanical | ~8 | EnginePropeller, Gearbox |
| hydraulic | ~5 | HydraulicPump, Accumulator |
| thermal | ~3 | Heater, Cooler |
| logical | ~10 | AND_gate, OR_gate, Timer |
| systems | ~15 | lamp_pass_through, simple_battery |
| visual | ~5 | Group, Label |
| (root) | ~9 | BlueprintInput, BlueprintOutput, RefNode |

---

## Phase 3: Editor Persistence v2

### Goal

Replace `blueprint_to_editor_json()` / `load_editor_format()` with v2 equivalents. The editor's `Blueprint` struct (nodes, wires, sub_blueprint_instances, viewport) serializes to/from `BlueprintV2`.

### New/Modified Files

- `src/v2/convert.h` / `src/v2/convert.cpp` — add `editor_blueprint_to_v2()` / `v2_to_editor_blueprint()`
- `src/editor/visual/scene/persist.cpp` — replace all functions with v2 wrappers
- `src/editor/visual/scene/persist.h` — update API
- `tests/test_persist.cpp` — update all 123+ tests to use v2 JSON

### Conversion Functions

```cpp
// Editor Blueprint → BlueprintV2 (for saving)
BlueprintV2 editor_blueprint_to_v2(const Blueprint& bp, const an24::TypeRegistry& registry);

// BlueprintV2 → Editor Blueprint (for loading)
Blueprint v2_to_editor_blueprint(const BlueprintV2& v2bp, const an24::TypeRegistry& registry);
```

Key mapping:
- `Blueprint.nodes` (vector) → `BlueprintV2.nodes` (map, key = node.id)
- `Blueprint.wires` → `BlueprintV2.wires` (WireEnd → WireEndV2)
- `Blueprint.sub_blueprint_instances` → `BlueprintV2.sub_blueprints`
  - `baked_in=false` → reference mode: `template` + `overrides`
  - `baked_in=true` → embedded mode: inline `nodes` + `wires`
- `Blueprint.pan/zoom/grid_step` → `BlueprintV2.viewport`
- Collapsed Blueprint nodes → NOT stored in `nodes` (they are reconstructed from `sub_blueprints` keys on load)
- Internal nodes of non-baked-in sub-blueprints → NOT stored (re-expanded from library on load, same as current behavior)

### Failing Tests First (15 tests)

```
TEST(PersistV2, SaveLoad_EmptyBlueprint)
```
Empty blueprint → save → load → assert empty nodes, empty wires.

```
TEST(PersistV2, SaveLoad_SingleNode)
```
One battery node → save → load → assert node fields match (id, type, pos, size, params).

```
TEST(PersistV2, SaveLoad_NodeContent)
```
Node with gauge content → save → load → assert content preserved.

```
TEST(PersistV2, SaveLoad_NodeColor)
```
Node with custom color → save → load → assert color preserved.

```
TEST(PersistV2, SaveLoad_Wire)
```
Two nodes + wire → save → load → assert wire endpoints, routing points.

```
TEST(PersistV2, SaveLoad_Viewport)
```
Set pan/zoom/grid → save → load → assert viewport matches.

```
TEST(PersistV2, SaveLoad_SubBlueprintReference)
```
Non-baked-in sub-blueprint → save → load → assert:
- Sub-blueprint entry has `template` path
- Internal nodes NOT in `nodes` map
- Overrides preserved
- After load, internal nodes re-expanded from registry

```
TEST(PersistV2, SaveLoad_SubBlueprintEmbedded)
```
Baked-in sub-blueprint → save → load → assert:
- Sub-blueprint entry has inline `nodes` and `wires`
- `template` kept for provenance
- All internal nodes present after load

```
TEST(PersistV2, SaveLoad_MixedSubBlueprints)
```
One reference + one embedded sub-blueprint → roundtrip.

```
TEST(PersistV2, SaveLoad_ParamsOverride)
```
Reference sub-blueprint with params override → save → load → verify params applied.

```
TEST(PersistV2, SaveLoad_LayoutOverride)
```
Reference sub-blueprint with layout override → save → load → verify positions.

```
TEST(PersistV2, NodesAsObject_NoDuplicates)
```
Attempt to add two nodes with same ID → second silently overwrites → save → load → only one node.

```
TEST(PersistV2, WireEndpointsAreArrays)
```
Save → parse raw JSON → verify wire `from`/`to` are arrays not strings.

```
TEST(PersistV2, PositionsAreArrays)
```
Save → parse raw JSON → verify node `pos` is array `[x, y]` not object `{x, y}`.

```
TEST(PersistV2, FileExtensionBlueprint)
```
`save_blueprint_to_file()` writes to `.blueprint` extension. `load_blueprint_from_file()` reads it.

### Implementation

1. **`src/v2/convert.cpp`** — add ~300 lines:
   - `editor_blueprint_to_v2()`: Iterate `Blueprint.nodes`, skip collapsed expandable nodes and non-baked-in internal nodes (same filter logic as current `blueprint_to_editor_json()`). Map `SubBlueprintInstance` → `SubBlueprintV2`.
   - `v2_to_editor_blueprint()`: Convert `BlueprintV2.nodes` map → vector of `Node`. Convert `SubBlueprintV2` → `SubBlueprintInstance`. Re-expand non-baked-in sub-blueprints from registry. Create collapsed expandable nodes. Rebuild wire index, recompute group IDs.

2. **`src/editor/visual/scene/persist.cpp`** — gut and rewrite (~200 lines, down from ~876):
   - `blueprint_to_editor_json()` → calls `editor_blueprint_to_v2()` then `serialize_blueprint_v2()`
   - `load_editor_format()` → calls `parse_blueprint_v2()` then `v2_to_editor_blueprint()`
   - `save_blueprint_to_file()` / `load_blueprint_from_file()` — trivial file I/O wrappers
   - Delete `blueprint_to_json()` (moved to Phase 4)
   - Delete all v1 parsing/serialization helpers

3. **`src/editor/visual/scene/persist.h`** — simplify API:
   ```cpp
   #pragma once
   #include "data/blueprint.h"
   #include <string>
   #include <optional>

   /// Serialize Blueprint to v2 JSON string
   std::string blueprint_to_editor_json(const Blueprint& bp);

   /// Deserialize Blueprint from v2 JSON string
   std::optional<Blueprint> blueprint_from_json(const std::string& json);

   /// Save to .blueprint file
   [[nodiscard]] bool save_blueprint_to_file(const Blueprint& bp, const char* path);

   /// Load from .blueprint file
   [[nodiscard]] std::optional<Blueprint> load_blueprint_from_file(const char* path);
   ```

4. **`tests/test_persist.cpp`** — update all 123+ tests:
   - Change all inline JSON strings from v1 format to v2 format
   - Remove tests for deleted v1 features (`collapsed_groups`, string wire endpoints, etc.)
   - Keep all behavioral tests (roundtrip, non-baked-in skip, re-expansion, etc.)

### persist.cpp Needs Registry Access

Current `load_editor_format()` already uses a static `TypeRegistry` (loaded once, cached). The v2 `v2_to_editor_blueprint()` also needs the registry for re-expanding non-baked-in sub-blueprints and for looking up port definitions. We maintain the same pattern: a module-level static registry, loaded lazily.

---

## Phase 4: Eliminate Blueprint→JSON→parse→build Roundtrip

### Goal

Replace the `SimulationController::build()` roundtrip:
```
Blueprint → blueprint_to_json() → JSON → parse_json() → ParserContext → build_systems_dev()
```

With a direct path:
```
Blueprint → extract_build_input() → BuildInput → build_systems_dev()
```

This eliminates the simulation JSON format entirely and lets us delete `blueprint_to_json()`.

### New/Modified Files

- `src/v2/build_input.h` / `src/v2/build_input.cpp` — `extract_build_input()`
- `src/editor/simulation.cpp` — use direct path
- `tests/test_simulation_build.cpp` — new tests for direct build

### Failing Tests First (6 tests)

```
TEST(BuildInput, ExtractDevicesFromBlueprint)
```
Blueprint with 3 nodes → `extract_build_input()` → assert 3 `DeviceInstance` entries with correct classname, params, ports.

```
TEST(BuildInput, ExtractConnectionsFromBlueprint)
```
Blueprint with 2 wires → extract → assert 2 connections with correct `from`/`to` in `"node.port"` format.

```
TEST(BuildInput, SkipsExpandableNodes)
```
Blueprint with collapsed expandable node → extract → assert expandable node NOT in devices list.

```
TEST(BuildInput, IncludesInternalNodes)
```
Blueprint with sub-blueprint internal nodes (`lamp_1:vin`, `lamp_1:lamp`) → extract → assert internal nodes present as devices.

```
TEST(BuildInput, PortAliasFromRegistry)
```
Blueprint node whose type has aliased ports → extract → assert `DeviceInstance.ports` has alias set.

```
TEST(BuildInput, SimulationResultsMatch)
```
Build same Blueprint both ways (old roundtrip vs new direct) → run 10 simulation steps → assert signal values identical. This is the gold-standard regression test. (Keep the old codepath temporarily for this comparison test, then delete.)

### Implementation

1. **`src/v2/build_input.h`** (~30 lines):
   ```cpp
   #pragma once
   #include "json_parser/json_parser.h"
   #include "data/blueprint.h"

   namespace an24::v2 {

   struct BuildInput {
       std::vector<DeviceInstance> devices;
       std::vector<std::pair<std::string, std::string>> connections;
   };

   /// Extract simulation build input directly from editor Blueprint.
   /// Skips expandable (collapsed) nodes, includes all internal nodes.
   /// Merges port/domain info from TypeRegistry.
   BuildInput extract_build_input(const Blueprint& bp, const TypeRegistry& registry);

   } // namespace an24::v2
   ```

2. **`src/v2/build_input.cpp`** (~100 lines):
   - Iterate `bp.nodes`, skip `expandable` nodes
   - For each node: create `DeviceInstance` with name=node.id, classname=node.type_name, params=node.params
   - Merge port definitions from registry (`merge_device_instance()` equivalent)
   - For each wire: create connection `"start.node_id.port_name" → "end.node_id.port_name"`
   - Handle BlueprintInput/BlueprintOutput wire rewriting (same logic as current `blueprint_to_json()` endpoint rewriting)

3. **`src/editor/simulation.cpp`** — simplify `build()`:
   ```cpp
   void SimulationController::build(const Blueprint& bp) {
       auto input = an24::v2::extract_build_input(bp, get_registry());
       build_result = build_systems_dev(input.devices, input.connections);
       // ... rest unchanged (allocate signals, init RefNodes)
   }
   ```

4. **Delete** `blueprint_to_json()` from persist.cpp and persist.h.

---

## Phase 5: Codegen v2 Integration

### Goal

Update AOT code generation (`codegen.cpp`) to work with v2 library format. Codegen reads `TypeRegistry` — which after Phase 2 loads from `.blueprint` files. The `TypeDefinition` struct is unchanged (Phase 2's `v2_to_type_definition()` converts back), so codegen may work with zero changes.

### Verification Tests (3 tests)

```
TEST(CodegenV2, GeneratesFromV2Registry)
```
Load v2 registry → run codegen for a simple circuit → assert generated C++ compiles and produces correct output.

```
TEST(CodegenV2, CompositeAotExpansion)
```
Load v2 registry with composite types → codegen generates nested System classes → assert expansion correct.

```
TEST(CodegenV2, PortAliasInAot)
```
Type with aliased ports → codegen → assert generated `Binding<>` references alias correctly.

### Implementation

Likely zero code changes needed — `codegen.cpp` consumes `TypeDefinition` and `DeviceInstance`, which Phase 2 ensures are correctly populated from v2 format. These tests verify that assumption.

If any issues arise, the fixes will be in `v2_to_type_definition()` conversion, not in codegen itself.

---

## Phase 6: Delete v1 Code + Cleanup

### Goal

Delete all v1 JSON code. Remove dead functions, dead fields, dead test helpers. Clean up includes.

### Deletions

**`src/editor/visual/scene/persist.cpp`** — delete:
- All v1 serialization helpers (already replaced in Phase 3)
- Static `TypeRegistry` caching if no longer needed
- `blueprint_to_json()` (already removed in Phase 4)

**`src/json_parser/json_parser.cpp`** — delete:
- `parse_json()` (both overloads) — no longer called
- `serialize_json()` — no longer called
- `parse_device()`, `parse_connection()`, `parse_template()`, `parse_subsystem()` — v1 helpers
- `merge_nested_blueprint()` — v1 expansion logic
- v1 `parse_type_definition()` — replaced by v2 loader
- `extract_exposed_ports()` — replaced by v2 `exposes`

**`src/json_parser/json_parser.h`** — delete:
- `ParserContext` struct (if no longer needed)
- `SystemTemplate`, `SubsystemCall` structs (v1 template system)
- `parse_json()`, `serialize_json()` declarations
- `extract_exposed_ports()` declaration

**`src/editor/data/blueprint.h`** — clean up:
- Remove `baked_in` from `SubBlueprintInstance` if fully migrated
- Evaluate whether `SubBlueprintInstance` can be replaced by `SubBlueprintV2` directly

**Tests** — delete:
- Any test that exercises v1 parsing/serialization exclusively
- Tests for `collapsed_groups`, string wire endpoints, v1 dedup

### Failing Tests First (2 tests)

```
TEST(V2Cleanup, NoParsejsonCalls)
```
Compile-time: ensure `parse_json()` is not called anywhere. (This is a grep check, not a runtime test — documented as a manual verification step.)

```
TEST(V2Cleanup, AllTestsStillPass)
```
Meta-test: run full `ctest` suite → all pass. This is the final gate.

---

## Phase 7: Convert Existing Save Files

### Goal

Provide a one-shot converter for any existing `.json` editor save files to `.blueprint` v2 format.

### Implementation

- `examples/convert_save.cpp` (~80 lines): reads a v1 editor save, loads via legacy `load_editor_format()` (temporarily kept), converts to v2 via `editor_blueprint_to_v2()`, writes `.blueprint`.
- Or: if all developers can simply re-save from the editor (which now writes v2), this phase is optional.

### Decision: This phase is optional if all save files can be recreated.

---

## Execution Order & Dependencies

```
Phase 1 ──→ Phase 2 ──→ Phase 3 ──→ Phase 4
  (v2 types)   (library)   (editor)   (direct build)
                              │
                              ↓
                           Phase 5 ──→ Phase 6
                           (codegen)   (cleanup)
                                          │
                                          ↓
                                       Phase 7
                                       (convert saves)
```

Phase 1 is prerequisite for all others.  
Phase 2 + 3 can be developed in parallel (different code paths), but Phase 3 tests need v2 library files.  
Phase 4 depends on Phase 3 (needs to know editor persist works before removing simulation format).  
Phase 5 can run after Phase 2 (just verification).  
Phase 6 is the final cleanup after everything works.  
Phase 7 is optional.

---

## Risk Mitigation

### Risk 1: Port Alias Breakage
**Mitigation**: Phase 2 test `PortAliasPreserved` explicitly tests alias roundtrip. Phase 5 test `PortAliasInAot` verifies codegen. Both must pass before cleanup.

### Risk 2: ~40 Test Targets Compile persist.cpp Directly
**Mitigation**: Phase 3 modifies persist.cpp's implementation but keeps the same header API. Callers don't change. Tests that inline v1 JSON need updating, but the C++ test code calling `save_blueprint_to_file()` / `load_blueprint_from_file()` does not.

### Risk 3: Wire Endpoint Rewriting for Collapsed Nodes
**Mitigation**: Phase 4's `extract_build_input()` must replicate the wire rewriting logic from `blueprint_to_json()`. The regression test `SimulationResultsMatch` compares both paths to catch any discrepancy.

### Risk 4: Static TypeRegistry in persist.cpp
**Mitigation**: v2 conversion functions take `TypeRegistry&` as a parameter. The static caching pattern in persist.cpp is preserved for the module-level wrappers that don't take a registry parameter.

---

## Phase Summary

| Phase | Scope | Tests | New Files | Modified Files |
|-------|-------|-------|-----------|----------------|
| 1 | v2 types + parser/serializer | 12 | blueprint_v2.h/cpp, test_blueprint_v2.cpp | CMakeLists.txt |
| 2 | Library conversion | 8 | convert.h/cpp, convert_library.cpp, test_library_v2.cpp | json_parser.cpp |
| 3 | Editor persistence v2 | 15 | — | persist.cpp, persist.h, test_persist.cpp |
| 4 | Direct build path | 6 | build_input.h/cpp, test_simulation_build.cpp | simulation.cpp |
| 5 | Codegen verification | 3 | test_codegen_v2.cpp | — (likely none) |
| 6 | Delete v1 code + cleanup | 2 | — | many (deletions) |
| 7 | Convert save files (optional) | 0 | convert_save.cpp | — |
| **Total** | | **46** | | |

## Key Functions Reference

| Function | File | Purpose |
|----------|------|---------|
| `blueprint_to_json()` | persist.cpp:83 | Simulation format (DELETE in Phase 4) |
| `blueprint_to_editor_json()` | persist.cpp:~283 | Editor save (REPLACE in Phase 3) |
| `load_editor_format()` | persist.cpp:~464 | Editor load (REPLACE in Phase 3) |
| `parse_type_definition()` | json_parser.cpp | Library loader (UPDATE in Phase 2) |
| `load_type_registry()` | json_parser.cpp | Registry init (UPDATE in Phase 2) |
| `parse_json()` | json_parser.cpp:282 | Simulation parser (DELETE in Phase 6) |
| `build_systems_dev()` | jit_solver.h | JIT build (UNCHANGED — caller changes) |
| `expand_type_definition()` | blueprint.cpp:~259 | TypeDef→Blueprint (KEEP, used by Phase 3) |
| `SimulationController::build()` | simulation.cpp:9 | Build trigger (SIMPLIFY in Phase 4) |
| `Document::save()/load()` | document.cpp:23/40 | File I/O (UNCHANGED — delegates to persist) |
| `Codegen::generate()` | codegen.cpp | AOT gen (UNCHANGED — consumes TypeDef) |
