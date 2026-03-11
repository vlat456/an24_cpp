# An-24 JSON Format v2 Migration — Context

## What This Is

Replacing all JSON persistence in the An-24 flight simulation with a unified v2 format where "Everything is a Blueprint" — components, systems, and root documents share the same schema. Clean break, no backward compatibility.

- Design doc: `JSON_FORMAT_V2_DESIGN.md`
- Implementation plan: `V2_IMPL_PLAN.md`
- Code style: `AGENTS.md`

---

## Phase Overview

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | v2 data structures + parse/serialize | **DONE** |
| 2 | Library `.json` → `.blueprint` conversion | **DONE** |
| 3 | Editor persistence v2 | **IN PROGRESS** — production code done, 34 tests to fix |
| 4 | Direct Blueprint→BuildInput (eliminate simulation JSON) | Not started |
| 5 | Codegen verification | Not started |
| 6 | Delete v1 code + cleanup | Not started |
| 7 | Convert save files (optional) | Not started |

---

## Current State (as of 2026-03-11)

**Build: PASSES** — all production code compiles cleanly.

**Tests: 1320 pass, 34 fail, 1 skipped** out of 1354 total.

All 34 failures are tests with hardcoded v1 JSON that need mechanical conversion to v2 format. No production code changes remain for Phase 3.

---

## Phase 1: COMPLETE

v2 data structures and standalone parse/serialize.

**Files created:**
- `src/v2/blueprint_v2.h` (170 lines) — `BlueprintV2`, `NodeV2`, `WireV2`, `SubBlueprintV2`, etc.
- `src/v2/blueprint_v2.cpp` (548 lines) — `parse_blueprint_v2()` + `serialize_blueprint_v2()`
- `tests/test_blueprint_v2.cpp` (647 lines) — 18 tests, all passing

**Key types:**
- `NodeV2` has 5 editor-only fields: `display_name`, `render_hint`, `expandable`, `group_id`, `blueprint_path`
- `SubBlueprintV2` has `type_name` field
- `BlueprintV2.nodes` is `map<string, NodeV2>` (object keyed by id)
- Wire endpoints are `WireEndV2{node, port}` (serialized as `["node", "port"]`)

---

## Phase 2: COMPLETE

Library conversion from v1 `.json` to v2 `.blueprint`.

**Files created/modified:**
- `src/v2/convert.h` (22 lines) — declares `type_definition_to_v2()` + `v2_to_type_definition()`
- `src/v2/convert.cpp` (305 lines) — conversion implementations
- `tests/test_library_v2.cpp` (470 lines) — 8 tests, all passing

**What happened:**
- All 70 library `.json` files deleted, replaced with 70 `.blueprint` files in v2 format
- `load_type_registry()` in `json_parser.cpp` now scans for `*.blueprint` files
- `parse_type_definition()` uses `parse_blueprint_v2()` → `v2_to_type_definition()`

**Library directory:** `library/` — 70 `.blueprint` files across 9 subdirectories + 1 `blueprint.json` (editor save file, still v1 format, convert in Phase 7)

---

## Phase 3: IN PROGRESS — Test Updates Remaining

### Production Code: DONE

All persist.cpp changes are complete:

**`src/editor/visual/scene/persist.cpp`** (818 lines) — heavily rewritten:
- `editor_blueprint_to_v2(const Blueprint&)` — converts editor Blueprint to BlueprintV2
- `v2_to_editor_blueprint(const BlueprintV2&)` — converts BlueprintV2 back to editor Blueprint
- `blueprint_to_editor_json()` now calls `editor_blueprint_to_v2()` → `serialize_blueprint_v2()`
- `blueprint_from_json()` now calls `parse_blueprint_v2()` → `v2_to_editor_blueprint()`
- **v1 loading completely removed** — v1 JSON returns `nullopt`
- Uses function-local static `TypeRegistry` (lazy-loaded, cached)
- `blueprint_to_json()` (simulation format) LEFT UNCHANGED (deleted in Phase 4)

**`src/editor/visual/scene/persist.h`** (20 lines) — API unchanged:
```cpp
std::string blueprint_to_editor_json(const Blueprint& bp);
std::optional<Blueprint> blueprint_from_json(const std::string& json_str);
```

### What Remains: 34 Failing Tests

Every failing test either feeds hardcoded v1 JSON to `blueprint_from_json()` (which now rejects it), or checks v1 keys/structure in the output of `blueprint_to_editor_json()`.

The work is purely mechanical: convert test JSON literals from v1 to v2 format, and update assertions that check output structure.

---

### v1 → v2 Format Mapping (for converting test JSON)

| v1 (old) | v2 (new) |
|----------|----------|
| `"devices": [{"name": "x", "classname": "Y", ...}]` (array) | `"nodes": {"x": {"type": "Y", ...}}` (object keyed by id) |
| `"from": "a.v_out"` (dot string) | `"from": ["a", "v_out"]` (2-element array) |
| `"to": "b.v_in"` | `"to": ["b", "v_in"]` |
| `"routing_points": [{"x":1,"y":2}]` | `"routing": [[1,2]]` |
| `"pos": {"x":1,"y":2}` / `"size": {"x":3,"y":4}` | `"pos": [1,2]` / `"size": [3,4]` |
| `"viewport": {"pan": {"x":0,"y":0}, "zoom": 1.0, "grid_step": 16}` | `"viewport": {"pan": [0,0], "zoom": 1.0, "grid": 16}` |
| `"sub_blueprint_instances": [{"id": "x", ...}]` (array) | `"sub_blueprints": {"x": {...}}` (object keyed by id) |
| `"baked_in": true/false` | Implicit: presence of `"nodes"` key = embedded |
| `"internal_node_ids": [...]` | Not saved (reconstructed from `group_id` on nodes) |
| No `"version"` or `"meta"` | `"version": 2`, `"meta": {"name": ""}` required |
| Ports saved inline per device | Ports NOT saved — reconstructed from TypeRegistry on load |
| `"kind": "Node"` | Not used |
| `"params_override"` / `"layout_override"` / `"internal_routing"` on SBI | `"overrides": {"params": {...}, "layout": {...}, "routing": {...}}` |
| `"blueprint_path"` on SBI | `"template"` on sub-blueprint |
| `"classname"` on device | `"type"` on node |
| `"name"` on device (the ID) | Key of the node in `"nodes"` object |
| Content `"type": 1` (integer) | `"kind": "Gauge"` (string). `"HoldButton"` maps to `NodeContentType::Switch` |

### v2 Output Structure (for tests checking JSON output)

When tests parse output of `blueprint_to_editor_json()` and inspect JSON structure:
- `j["nodes"]` is an **object** keyed by node id (not an array)
- `j["nodes"].size()` gives count
- `j["nodes"].contains("x")` to check existence
- `j["nodes"]["x"]["type"]` for type
- `j["sub_blueprints"]` is an **object** keyed by SBI id (not an array)
- `j["sub_blueprints"]["lamp_1"]["template"]` for path
- `j["sub_blueprints"]["lamp_1"]["overrides"]["params"]` for overrides
- Non-baked-in: NO `"nodes"` key inside sub-blueprint
- Baked-in: HAS `"nodes"` key inside sub-blueprint
- `j["wires"][i]["from"]` is `["node_id", "port_name"]` array
- `j["wires"][i]["routing"]` (not `"routing_points"`)
- `j["viewport"]["grid"]` (not `"grid_step"`)
- `j["viewport"]["pan"]` is `[x, y]` array

---

### Failing Tests — Complete List

#### `tests/test_node_color.cpp` — 5 tests (converted to v2 JSON but still failing at runtime)

These were converted in a previous session. The v2 JSON may have issues — debug by running:
```bash
cd build && ctest -R "NodeColor" --output-on-failure
```

- `NodeColorPersist.LoadFromJson_WithColor`
- `NodeColorPersist.LoadFromJson_WithoutColor`
- `NodeColorPersist.MalformedColor_String_Ignored`
- `NodeColorPersist.MalformedColor_Null_Ignored`
- `NodeColorPersist.PartialColor_DefaultsApplied`

#### `tests/test_params_integrity.cpp` — 3 tests (NOT yet converted)

- `ParamsIntegrity.LoadedBlueprintHasFullParams` (line 69)
- `ParamsIntegrity.UserOverridesPreservedOnLoad` (line 107)
- `ParamsIntegrity.ComponentWithNoDefaultParams_StaysEmpty` (line 184)

#### `tests/test_persist.cpp` — 23 tests (NONE converted)

**Tests with inline v1 JSON input:**
- `PersistTest.EditorFormat_WithMetadata` (line 181)
- `PersistTest.DuplicateNodes_DedupedOnLoad` (line 721) — builds v1 JSON via nlohmann API
- `PersistTest.DedupGuard_DuplicateWiresDroppedOnLoad` (line 865)
- `PersistTest.DedupGuard_DuplicateNodesDroppedOnLoad` (line 890)
- `PersistTest.DedupGuard_DuplicateRoutingPointsDroppedOnLoad` (line 910)
- `PersistTest.RenderHint_RefNode` (line 1426)
- `PersistTest.RenderHint_Bus` (line 1444)
- `PersistTest.Expandable_Blueprint` (line 1461)
- `PersistTest.UnknownPortType_DoesNotCrash` (line 1518) — **v2 doesn't store ports; rethink**
- `PersistTest.MissingPortType_DoesNotCrash` (line 1537) — **same**

**Tests checking JSON output structure (v1 keys → need v2 keys):**
- `PersistNonBakedIn.Save_SkipsInternalNodes` (line 1782)
- `PersistNonBakedIn.Save_SkipsInternalWires` (line 1835)
- `PersistNonBakedIn.Save_BakedInStillSavesInternals` (line 1910)
- `PersistNonBakedIn.Save_PreservesSubBlueprintInstanceMetadata` (line 1976)
- `PersistNonBakedIn.BlueprintPath_ContainsCategory` (line 2235)
- `PersistNonBakedIn.Roundtrip_PreservesInternalNodePositions` (line 2292)
- `PersistTest.EditorSave_DedupsSubBlueprints` (line 1182)

**Tests with inline v1 JSON for load:**
- `PersistNonBakedIn.Load_ReExpandsFromRegistry` (line 2009)
- `PersistNonBakedIn.Load_AppliesParamsOverride` (line 2047)
- `PersistNonBakedIn.Load_AppliesLayoutOverride` (line 2078)
- `PersistNonBakedIn.Load_AutoLayoutFallback_WhenNoLayoutOverride` (line 2376)
- `SubBlueprintMenu.EditOriginal_ResolvesLibraryPath` (line 2258)

#### `tests/test_editor_hierarchical.cpp` — 4 tests (NONE converted)

- `EditorPersistence.AddedSubNodePersistsRoundtrip` (line 1404) — checks `j["devices"]`, `j["wires"]`
- `EditorPersistence.EditorFormatRoundtrip` (line 1514) — checks `j["devices"]`, `j["wires"]`
- `BlueprintSignalFlow.BlueprintJsonFile_SOR_Stability` (line 1782) — loads `library/blueprint.json` (v1 file)
- `BlueprintSignalFlow.BlueprintJsonFile_JIT_Simulator` (line 1906) — loads `library/blueprint.json` (v1 file)

### Special Cases Requiring Thought

1. **`UnknownPortType_DoesNotCrash` and `MissingPortType_DoesNotCrash`**: v2 doesn't store ports in JSON. Ports come from TypeRegistry on load. Rewrite to verify that unknown `type` in a node doesn't crash (node just won't get ports), OR delete.

2. **`BlueprintJsonFile_SOR_Stability` and `BlueprintJsonFile_JIT_Simulator`**: Load `library/blueprint.json` (v1 format). Options: (a) convert that file to v2 now, (b) skip until Phase 7.

3. **Dedup tests**: v2 uses object keys (inherently unique for nodes/sub-blueprints). Node/SBI dedup tests may be simplified or deleted. Wire routing point dedup within a wire still applies.

---

## Phases 4-7 (Not Started)

### Phase 4: Eliminate Blueprint→JSON→parse→build Roundtrip

Currently `SimulationController::build()` does:
```
Blueprint → blueprint_to_json() → JSON string → parse_json() → ParserContext → build_systems_dev()
```

Replace with direct:
```
Blueprint → extract_build_input() → BuildInput → build_systems_dev()
```

This eliminates the simulation JSON format entirely and lets us delete `blueprint_to_json()`.

New files: `src/v2/build_input.h`, `src/v2/build_input.cpp`, `tests/test_simulation_build.cpp`
Modified: `src/editor/simulation.cpp`

### Phase 5: Codegen Verification

Verify AOT codegen works with v2 library format. Likely zero code changes — codegen consumes `TypeDefinition` which Phase 2 ensures is correctly populated.

### Phase 6: Delete v1 Code + Cleanup

Delete `parse_json()`, `serialize_json()`, all v1 helpers, dead fields, dead structs. Clean imports.

### Phase 7: Convert Save Files (Optional)

Convert `library/blueprint.json` and any other v1 save files to v2 `.blueprint`.

---

## Key Architecture Details

### Three JSON Formats Being Unified

1. **Library Type Definition** — `library/**/*.blueprint` → `TypeRegistry` — **DONE**
2. **Simulation Blueprint** — `blueprint_to_json()` → `parse_json()` → `build_systems_dev()` — Phase 4 eliminates
3. **Editor Save** — `blueprint_to_editor_json()` / `blueprint_from_json()` — **Phase 3, implementation done**

### Circular Dependency Resolution

`convert.cpp` depends on BOTH `json_parser.h` and `blueprint_v2.h`:
- `blueprint_v2` CMake library: ONLY `blueprint_v2.cpp` — standalone
- `json_parser` CMake library: includes BOTH `json_parser.cpp` AND `src/v2/convert.cpp` — links PUBLIC to `blueprint_v2`
- LSP shows spurious errors in `convert.cpp` — NOT real build errors

### PortDirection Enum

`an24::PortDirection` is `enum class { In, Out, InOut }`. Compare with `an24::PortDirection::In`, not `"In"`.

### Static TypeRegistry in persist.cpp

```cpp
static an24::TypeRegistry registry = an24::load_type_registry();
```
Lazy-loaded on first call, cached forever. Used for: enriching port types, filling missing params, looking up render_hint, re-expanding non-baked-in sub-blueprints.

### Port `alias` Is Critical

Union-find signal merging in JIT/AOT depends on port aliases. v2 preserves this. Bus has `"alias": "dynamic"` for multi-wire support.

### `expand_sub_blueprint_references()`

Recursively flattens nested sub-blueprints by prefixing device names with `ref.id + ":"`.

---

## File Reference

### Production Code
| File | Lines | Purpose |
|------|-------|---------|
| `src/v2/blueprint_v2.h` | 170 | v2 data structures |
| `src/v2/blueprint_v2.cpp` | 548 | parse/serialize |
| `src/v2/convert.h` | 22 | conversion declarations |
| `src/v2/convert.cpp` | 305 | TypeDef ↔ BlueprintV2, editor Blueprint ↔ BlueprintV2 |
| `src/editor/visual/scene/persist.cpp` | 818 | Editor save/load (v2 complete) |
| `src/editor/visual/scene/persist.h` | 20 | Public API (unchanged) |
| `src/json_parser/json_parser.cpp` | — | Registry loading (updated for v2) |
| `src/editor/data/blueprint.h` | — | Blueprint, SubBlueprintInstance structs |
| `src/editor/data/node.h` | — | Node, NodeContent, NodeColor, Port |
| `src/editor/data/wire.h` | — | Wire, WireEnd, WireKey |

### Test Files
| File | Lines | Status |
|------|-------|--------|
| `tests/test_blueprint_v2.cpp` | 647 | All passing |
| `tests/test_library_v2.cpp` | 470 | All passing |
| `tests/test_node_color.cpp` | 539 | 5 tests — converted but still failing, needs debug |
| `tests/test_params_integrity.cpp` | 266 | 3 tests — need v1→v2 conversion |
| `tests/test_persist.cpp` | 2417 | 23 tests — need v1→v2 conversion |
| `tests/test_editor_hierarchical.cpp` | 2022 | 4 tests — need updating |

---

## Build & Test Commands

```bash
# Full build + test
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(sysctl -n hw.ncpu) && cd build && ctest --output-on-failure

# Run specific test suite
cd build && ctest -R "NodeColor" --output-on-failure

# Run single test from executable
cd build/tests && ./persist_tests --gtest_filter="PersistTest.EditorFormat_WithMetadata"
```

Tests run from `build/tests/` directory, so `library/` paths need `../../library/` prefix.

---

## Design Decisions (All Resolved)

| Decision | Choice |
|----------|--------|
| External ports field | `exposes` |
| Wire IDs | Keep |
| Template path format | Full: `library/systems/lamp_pass_through` |
| Provenance in embedded | Keep `template` field |
| Viewport | Root documents only |
| File extension | `.blueprint` |
| Nodes container | Object (key=id) |
| Positions | Arrays `[x, y]` |
| Wire endpoints | Arrays `["node", "port"]` |
| baked_in flag | Eliminated — presence of `nodes` = embedded |
| Override keys | Unprefixed (scope is structural) |
| Content type | String (`"Gauge"`, `"Switch"`) not integer |
