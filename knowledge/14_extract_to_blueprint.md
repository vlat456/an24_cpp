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
- Root group only (`group_id == ""`)
- Selection size >= 2
- Selected items are regular nodes in current blueprint
- Internal wires preserved
- Boundary wires become interface ports
- Inline embedded nested definition created (`embedded = true`)
- Collapsed expandable node created in parent
- Undo/redo as one checkpoint (single `replace_current`)

### Excluded (follow-up)
- Sub-window/non-root group extraction
- Extracting existing nested nodes specially
- Existing BlueprintInput/BlueprintOutput special handling
- Auto-layout sophistication
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
    float center_x = 0.0f;
    float center_y = 0.0f;
};

struct CmdExtractToBlueprint {
    std::vector<ui::InternedId> selected_node_ids;
    std::string new_blueprint_name;
    std::string group_id; // MVP requires empty
};
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
  - group is root (MVP)

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

## Tests (MVP Required)

1. `CommandTest.ExtractToBlueprint_BasicAtomic`
   - select 2 nodes with one input and one output boundary
   - after extract: 1 collapsed node + 1 nested + rewired externals

2. `CommandTest.ExtractToBlueprint_UndoRedoRoundTrip`
   - checksum before/after, undo returns exact original

3. `CommandTest.ExtractToBlueprint_RejectsNonRootGroup`

4. `CommandTest.ExtractToBlueprint_RejectsSmallSelection`

5. `CommandTest.ExtractToBlueprint_DeterministicIfaceNaming`
   - repeated execution on same graph yields same generated iface names/order

---

## Known Follow-Ups

- Sub-group extraction with scoped paths
- nested-within-selection handling policy
- richer bridge node layout
- optional preview UX
