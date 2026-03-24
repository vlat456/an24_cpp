# Extract To Blueprint (Atomic MVP)

## Goal

Implement a strict, atomic MVP of **Extract to Blueprint** with predictable behavior and no implicit inference.

The operation must be a **single command transaction** over `bp2::Blueprint`:
- analyze selection
- build extracted inline blueprint
- replace selection with one collapsed nested instance
- reconnect boundary wires through explicit interface ports
- validate invariants before apply

If any precondition fails, the command must abort with no partial mutations.

---

## MVP Scope (deliberately narrow)

### Included
- Root and sub-group extraction (`group_id` must match all selected nodes)
- Selection size >= 2
- Selected items may include embedded nested instances (non-embedded refs are rejected)
- Internal wires preserved
- Boundary wires become interface ports
- Inline embedded nested definition created (`embedded = true`)
- Collapsed expandable node created in parent
- Undo/redo as one checkpoint (single `replace_current`)
- Preview API with name validation and iface conflict reporting
- Embedded nested selection allowed only when embedded descendants are transitively embedded (non-embedded descendants are rejected)
- Advanced guarded mode available (`allow_nonembedded_descendant_refs=true`) to bypass descendant strict rejection
- In guarded mode, non-embedded descendants are remapped to embedded inline defs when a matching embedded source nested exists by `blueprint_id`

### Excluded (follow-up)
- Any inference/coercion beyond explicit wire endpoint metadata

---

## Non-Negotiable Constraints

1. **Atomic apply**
   - Build full `updated_bp` in memory.
   - Validate with invariants.
   - Apply once via `model.replace_current(updated_bp)`.
   - Never do incremental remove/add mutation sequence.

2. **No inference**
   - Domain comes from boundary wire domain.
   - Port direction comes from boundary side classification.
   - No fallback like "prefer Electrical".
   - No type guessing from param text.

3. **Deterministic naming/order**
   - Stable boundary ordering by `(wire_id, source_path, target_path)`.
   - Deterministic dedup suffixing (`_2`, `_3`, ...).

4. **Strict failure**
   - Any unresolved endpoint, duplicate generated ID, or validation failure aborts operation.

---

## Data Model

```cpp
struct ExternalConnection {
    bool is_input;                    // outside->inside
    ui::InternedId external_node_id;
    ui::InternedId external_port;
    ui::InternedId internal_node_id;
    ui::InternedId internal_port;
    std::string iface_name;           // deterministic deduped name
    Domain domain;                    // copied from original wire
    ui::InternedId original_wire_id;
};

struct ExtractionPlan {
    std::vector<bp2::Blueprint::Node> internal_nodes;
    std::vector<bp2::Blueprint::Wire> internal_wires;
    std::vector<ExternalConnection> inputs;
    std::vector<ExternalConnection> outputs;
    std::unordered_set<ui::InternedId> selected_set;
    float min_x = 0.0f;    // bounding box of selected nodes
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    float center_x = 0.0f;
    float center_y = 0.0f;
};

// No CmdExtractToBlueprint struct — extraction is implemented as
// free functions that compute and return the new blueprint atomically:
//   build_extracted_blueprint_atomic(source, selected, name, group, interner, arena)
//   build_extract_to_blueprint_preview(source, selected, name, group, interner, arena)
```

---

## Algorithm (Atomic)

1. Validate command preconditions.
2. Analyze selection:
   - collect selected nodes
   - classify wires: internal / input boundary / output boundary
   - compute center
   - deterministic boundary order + iface name dedup
3. Build extracted inline blueprint:
   - copy internal nodes (translated to local coordinates)
   - create interface from boundary connections
   - add internal wires
   - add bridge nodes:
     - `BlueprintInput` nodes for inputs
     - `BlueprintOutput` nodes for outputs
   - add bridge wires from interface bridge nodes to internal endpoints
4. Build parent replacement blueprint:
   - remove selected nodes
   - remove all wires touching selection
   - add `Nested` inline def
   - add collapsed node (same id as nested instance)
   - reconnect external boundary wires to collapsed ports
5. Run invariant validation on resulting blueprint.
6. Apply once (`replace_current`).

---

## UI Contract

### Context menu
- Show `Extract to Blueprint...` only when:
  - not read-only
  - active selection size >= 2

### Dialog
- Modal asks for blueprint name.
- Default: `extracted_blueprint_N`.
- On confirm: execute command wrapped by checkpoint.

---

## File Plan

### New
- `src/editor/commands/extract_blueprint.h`
- `src/editor/commands/extract_blueprint.cpp`
- `src/editor/visual/popups/extract_to_blueprint_dialog.h`

### Modify
- `src/editor/commands/commands.h` (add `CmdExtractToBlueprint`)
- `src/editor/commands/commands.cpp` (execute handler)
- `src/editor/document.h/.cpp` (wrapper API)
- `src/editor/window_system.h/.cpp` (pending dialog state)
- `src/editor/visual/popups/context_menus.cpp` (menu entry)
- `src/editor/app/editor_app.h/.cpp` (render dialog)
- `examples/CMakeLists.txt` (compile new source if needed)
- `tests/test_commands.cpp` (atomic command regression)

---

## Tests (Current Required)

1. `CommandTest.ExtractToBlueprint_BasicAtomic`
   - select 2 nodes with one input and one output boundary
   - after extract: 1 collapsed node + 1 nested + rewired externals

2. `CommandTest.ExtractToBlueprint_UndoRedoRoundTrip`
   - checksum before/after, undo returns exact original

3. `CommandTest.ExtractToBlueprint_AllowsSubgroupExtraction`

4. `CommandTest.ExtractToBlueprint_RejectsSmallSelection`

5. `CommandTest.ExtractToBlueprint_DeterministicIfaceNaming`
   - repeated execution on same graph yields same generated iface names/order

### Additional regression/edge-case tests

- `UsesCanonicalBridgeNodeIds` — bridge node ID pattern `instance:iface_name`
- `InlineBlueprintStructure` — interface, bridge port layout, internal nodes in inline_def
- `SubgroupBridgeWiring` — bridge wires correctly wired in parent subgroup
- `RejectsInputOutputIfaceNameCollision` — fail-fast on cross-direction port name collision
- `DedupeNameNoCollisionWithSuffixedPorts` — "in", "in_2" dedup regression
- `ZeroExternalConnections` — fully internal selection produces empty interface
- `InlineBridgeYUsesLocalCoordinates` — bridge Y uses translated coordinates
- `BridgeAutoLayoutTracksInternalY` — bridge Y ordering follows internal node positions
- `AllowsEmbeddedNestedInstanceSelection` — embedded nested instances preserved in inline_def
- `InlinesSelectedEmbeddedNestedDeterministically` — nested merge order is deterministic (independent interner runs)
- `RejectsEmbeddedNestedMissingInlineDef` — fail-fast for embedded nested without inline_def
- `RejectsNonEmbeddedNestedInstanceSelection` — non-embedded refs are rejected
- `PreviewBasic`, `PreviewReportsIfaceCollision`, `PreviewRejectsEmptyName`, `PreviewRejectsDuplicateName`
- `PreviewAllowsEmbeddedNestedSelection`, `PreviewRejectsNonEmbeddedNestedSelection`

---

## Known Follow-Ups

- deeper nested flatten/merge strategies (e.g. remapping non-embedded descendants instead of strict rejection)
