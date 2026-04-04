# KnobSwitch Clarification Plan

## Goal

Clarify `KnobSwitch` so users can use it for both:

1. `1 -> N` selection
2. `N -> 1` selection

without falsely encoding electrical direction into a passive rotary contact.

## Design Decision

- Keep `KnobSwitch` electrically passive.
- Do not make electrical terminals semantic `Input`/`Output`.
- Treat the part as `wiper + throws`, not `source + destinations`.
- Keep current wire highlighting behavior as voltage-based and domain-agnostic.

## PR 1: Semantics And Docs Cleanup

### Scope

- Document `KnobSwitch` as a passive selector.
- Explicitly state that `1 -> N` and `N -> 1` are the same electrical topology.
- Standardize terminology:
  - Port names are `wiper`, `throw1..throw5`

### Files

- `knowledge/03_components.md`
- `knowledge/07_library.md`
- `knowledge/05_editor.md`
- `knowledge/10_quick_reference.md`
- `library/electrical/KnobSwitch.blueprint`
- `src/jit_solver/components/knob_switch.h`

### Tasks

- Update blueprint description to describe passive selector semantics.
- Update component header comments to stop implying directional flow.
- Add a short note in knowledge docs:
  - `wiper -> throwN` and `throwN -> wiper` are both valid.
  - Direction is a user interpretation, not solver behavior.
- Add note that `InOut` here is a modeling compromise for passive contacts, not a dataflow meaning.

### Tests

- None required beyond existing suite if code behavior is unchanged.
- If docs/tests exist for descriptions, update those snapshots.

### Acceptance

- No code behavior changes.
- Docs consistently describe `KnobSwitch` as passive.
- No mention that it is specifically "1 input many outputs".

## PR 2: Non-Breaking UI Label Clarification

### Scope

- Use strict naming (`wiper`, `throw1..throw5`) across schema, runtime, and editor.
- No legacy aliases/fallback labels.

### Files

- Likely editor port rendering / inspector files:
  - `src/editor/visual/node/visual_node.cpp`
  - `src/editor/visual/port/visual_port.cpp`
  - `src/editor/visual/inspector/*`
  - any node/port label helpers already used in editor
- Possibly `src/editor/document.cpp` if labels come from node metadata

### Tasks

- Rename terminal ports in schema/runtime/editor to `wiper`, `throw1..throw5`.
- Remove old `common`/`t1..t5` naming from active paths.
- Ensure inspector, hover text, and node labels use new names consistently.

### Tests

- Add editor tests verifying `wiper` / `throw*` labels for `KnobSwitch`.
- Add regression test that runtime signal keys resolve using `wiper`, `throw1..throw5`.

### Acceptance

- Runtime/build/simulation use `wiper`, `throw1..throw5` only.
- UI no longer suggests directional source/sink semantics.

## PR 3: Optional User-Facing Variants

### Scope

- Add two user-facing library entries if the team wants clearer insertion choices:
  - `RotarySwitch1ToN`
  - `RotarySwitchNTo1`
- Both map to the same underlying passive implementation.

### Files

- `library/electrical/`
- registry/category metadata if needed
- editor insertion/menu code only if component aliases are not automatic

### Tasks

- Decide whether aliases are true new library entries or editor-only insertion aliases.
- Keep the same runtime component underneath.
- Use descriptions tailored to user intent:
  - "Select one destination from a common input"
  - "Select one source into a common output"
- Preserve neutral electrical semantics internally.

### Tests

- Library/type registry loads both variants.
- Inserted nodes build and simulate exactly like `KnobSwitch`.
- No duplicated runtime code.

### Acceptance

- Users can choose a topology-oriented variant from the palette.
- Internals remain passive and shared.

## PR 4: Editor Layout Hardening For Passive Multi-Terminal Parts

### Scope

- Make sure passive terminal families do not regress visually again.

### Files

- `src/editor/visual/node/visual_node.cpp`
- `tests/test_scene_mutations.cpp`
- `tests/test_properties_window.cpp` if needed

### Tasks

- Re-check `KnobSwitch` layout assumptions after label changes.
- Keep `InOut` terminals rendered once only.
- Ensure increasing positions `2 -> 5` still updates tick marks and terminal visibility correctly.
- Ensure palette insertion / inspector edits do not create left-right duplication.

### Tests

- Existing `KnobSwitch` regressions stay green.
- Add one more regression covering label rendering plus no duplicated terminals.

### Acceptance

- No visual duplication.
- No stale widget rebuild behavior.
- UI remains clear after PR 2/3.

## PR 5: Migration Decision Gate

### Scope

- Only do this if the team later wants actual persisted port renames.

### Tasks

- Already decided: persisted keys changed to `wiper` / `throw1..throw5`.
- No migration/fallback layer required in active development mode.

### Recommendation

- Keep strict naming without compatibility aliases.

## Agent Notes

For all coding agents:

- Do not change solver semantics.
- Do not reinterpret `KnobSwitch` as directed flow.
- Do not make wire highlighting electrical-current-based.
- Preserve backward compatibility for saved blueprints unless explicitly assigned the migration PR.
- Keep changes minimal and local.

## Recommended Order

1. PR 1
2. PR 2
3. PR 4
4. PR 3 only if palette aliases are still wanted
5. PR 5 only if the team later chooses schema migration

## Completion Status

- PR 1: Done
- PR 2: Done (strict naming, no fallback aliases)
- PR 3: Done (added `RotarySwitch1ToN`, `RotarySwitchNTo1`)
- PR 4: Done (layout and regression coverage)
- PR 5: Done (decision recorded and enforced)

### Final Verification

- Full suite: 1456/1456 passed after completion.
