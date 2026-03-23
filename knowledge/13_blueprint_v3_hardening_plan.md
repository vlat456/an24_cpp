# Blueprint V3 Hardening Plan

## Goal

Make Blueprint V3 robust against data corruption, stale references, migration drift, and editor/runtime divergence while keeping workflows fast and diffs readable.

## Success Criteria

- No dangling wire endpoints after any edit, undo/redo, save/load cycle.
- Deterministic serialization (stable ordering, minimal noisy diffs).
- Backward-compatible migration path with explicit version contracts.
- Typed parameter handling (including non-numeric params like LUT tables) preserved through load/edit/save.
- Editor visual behavior (Bus aliases, reconnect/swap) matches serialized semantics and runtime topology.

## Guiding Principles

- One source of truth: `Blueprint::{nodes,wires,nested}`.
- Derived state only: indices/caches/UI aliases are recomputable.
- ID-based references over pointers for persistence and commands.
- Atomic commands with invariant checks at transaction boundaries.
- Strict schema + explicit migration, not permissive silent fallback for core fields.

## Phase 1: Invariant and Schema Foundation (High Priority)

### 1.1 Canonical Invariant Checker

Centralize all checks in one module (and call it consistently):

- Unique IDs for nodes, wires, nested refs.
- Every wire endpoint resolves to an existing node+port.
- No malformed paths (`Root -> Node -> Port` only for wire endpoints).
- Domain/type compatibility rules, including special bridge components.
- Nested blueprint port mapping validity.

Run points:

- After decode (load).
- After each command in debug builds.
- Before encode (save), at least in CI/tests.

### 1.2 JSON Schema by Version

Create explicit schemas for:

- `3.0` current format.
- Future versions (`3.1`, `4.0`) with compatibility policy.

Define strict required fields and typed constraints for:

- `id`, `display_name`, `name`, `nodes`, `wires`, `nested`.
- Endpoint path format.
- Coordinate objects (`position: {x,y}`), not legacy arrays.

### 1.3 Migration Contract

Require explicit migration functions:

- `v2 -> v3`, `v3.0 -> v3.1`, etc.
- Golden fixtures for each migration.

No hidden migration side effects in normal decode path.

## Phase 2: Command and Transaction Safety (High Priority)

### 2.1 Command Contracts

Codify atomic behavior for mutating commands:

- `RemoveNode` must remove connected wires (already addressed, keep as contract).
- `ReconnectWire` should mutate endpoint in-place when ID/order preservation matters.
- Bus alias operations should map to canonical port semantics in persisted data.

### 2.2 Transaction Boundaries

Enforce one user action = one checkpoint:

- No nested implicit checkpoints inside command internals.
- Undo/redo must fully restore data + derived indices + visuals.

### 2.3 Command Fuzz Tests

Add randomized test sequences:

- Add/delete/reconnect/swap/move/undo/redo loops.
- Validate invariants after each step.

## Phase 3: Parameter System Hardening (High Priority)

### 3.1 Typed Param Descriptors

Replace ad-hoc float/string handling with type descriptors:

- `number`, `string`, `bool`, `enum`, `table`, `vec2`, etc.
- Default values and validators in type definition metadata.

### 3.2 Unified Param Load/Edit/Save Pipeline

Guarantee parameter roundtrip fidelity:

- Preserve unknown/forward-compatible params where safe.
- Preserve non-numeric params (LUT table, text, enum strings).
- Keep display UI decoupled from raw storage format.

### 3.3 Regression Suite

Must include:

- LUT table visible/editable after load.
- Name/display_name persistence.
- Port layout + color + node content metadata persistence.

## Phase 4: Bus/Alias Semantics Unification (High Priority)

### 4.1 Canonical Bus Model

Define one persisted representation:

- Bus wires always serialize to canonical bus port (`v`).
- Alias ports are UI-only handles tied to wire IDs.

### 4.2 Interaction Correctness

Preserve legacy UX behavior through explicit tests:

- Alias reconnect targets selected wire, not first wire.
- Alias-to-alias reconnect swaps wire order correctly.
- Base bus port starts wire creation.

### 4.3 Runtime Topology Normalization

At build/runtime boundary, normalize any residual alias-like endpoint text safely to canonical bus signal mapping.

## Phase 5: Deterministic Serialization and Diff Hygiene (Medium Priority)

### 5.1 Stable Ordering

Stable sort before save:

- Nodes by ID.
- Wires by ID (or deterministic insertion key).
- Param keys lexicographically.

### 5.2 Canonical Formatting

- Consistent float formatting.
- Canonical object key ordering for frequently-changed sections.

Result: review-friendly diffs and fewer merge conflicts.

## Phase 6: Diagnostics and Self-Repair (Medium Priority)

### 6.1 Structured Validation Errors

Improve errors with location/context:

- File path, node/wire ID, endpoint path, violated invariant.

### 6.2 Optional Repair Tooling

Add non-destructive diagnostics mode:

- Report dangling wires/orphan nodes/invalid params.
- Optional explicit repair command (never automatic in save path).

## Test Matrix

Minimum CI gates for Blueprint V3:

- Codec roundtrip tests (including name/display_name/params).
- Editor command regression tests (delete/recreate, reconnect/swap).
- Scene rebuild tests for complex known blueprints (e.g., `GSC.blueprint`).
- Simulation integration tests for splitter/merger/bus behavior.
- Randomized command fuzz tests with invariant checks.

## Rollout Plan

1. **Phase 1 + Phase 2** first (safety baseline).
2. **Phase 3 + Phase 4** next (high-frequency user pain points).
3. **Phase 5** for maintainability and team productivity.
4. **Phase 6** for resilience and support tooling.

## Immediate Next Tasks (Short List)

1. Add a shared `validate_blueprint_integrity()` call in load/save and debug command boundary.
2. Complete name/display_name usage audit in all menus/dialogs.
3. Expand canvas interaction regressions beyond bus tests to generic multi-wire reconnect cases.
4. Add deterministic save ordering test with golden snapshot.
