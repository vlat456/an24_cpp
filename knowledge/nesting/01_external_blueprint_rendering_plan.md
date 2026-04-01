# External Blueprint Rendering Plan

## Decision

Choose the latter approach:

- do **not** auto-promote `expandable + blueprint_path` nodes into `nested[]` during document load
- add a **second rendering/opening mode** for external blueprint references

Reason:

- it preserves source-of-truth separation between a document and library assets
- it keeps reusable library blueprints reusable instead of silently rewriting them into embedded instances
- it scales better to arbitrary nesting depth
- it avoids hidden mutation at load time
- it matches the current meaning of `blueprint_path`: reference another blueprint, not inline-copy it

## Hard Requirement

There must be **100% AOT-JIT parity** for nesting/reference semantics.

Meaning:

- a blueprint reference that works in JIT/editor flow must behave the same after AOT/codegen flattening
- embedded vs external-reference hierarchy must not introduce different signal routing, port exposure, or bridge semantics between JIT and AOT
- editor-only rendering/runtime conveniences must not become a hidden simulation behavior difference

Practical rule:

- editor/view-layer support for external blueprint references may be implemented separately
- but simulation/export/flattening semantics must remain single-source and identical for both JIT and AOT paths

## Problem Statement

Today the editor has two different concepts mixed together:

- `nested[]` entries: in-document hierarchical content, rendered through `group_id`
- `Node { expandable=true, blueprint_path=... }`: external blueprint reference

The sub-window system only understands the first one.

That causes two classes of bugs:

- `Open in New Window` fails for referenced library blueprints because no `nested[]` entry exists
- editor rendering logic assumes all sub-window content already lives inside the current document model

## Target Model

Support two explicit subgraph sources.

### Mode 1: Embedded/Nested Content

Use existing behavior:

- source is `Blueprint::Nested`
- content is already in current document model
- rendered by filtering nodes/wires on `group_id`
- editable when embedded

### Mode 2: External Blueprint Reference

New behavior:

- source is a node with `expandable=true` and non-empty `blueprint_path`
- content is loaded from `library/<blueprint_path>.blueprint`
- content is rendered from that loaded blueprint directly, not from current document `group_id`
- read-only by default
- supports arbitrary nesting by recursively opening referenced blueprints the same way

## Architecture Direction

### 1. Separate "window source" from "group_id"

Current `BlueprintWindow` assumes:

- every non-root window is identified by `group_id`
- all rendering/input operates on `doc.model().current()`

That is too narrow.

Add an explicit window source concept.

Suggested shape:

```cpp
enum class BlueprintWindowMode {
    RootDocument,
    EmbeddedGroup,
    ExternalReference,
};
```

Window state should distinguish:

- owning document
- render mode
- `group_id` for embedded mode
- referenced blueprint path for external mode
- loaded `bp2::Blueprint` snapshot for external mode

Do not overload `group_id` to mean both embedded group and external reference id.

### 2. Make rendering operate on a blueprint source

Current rendering helpers assume:

- `doc.blueprint()` is always the source
- non-root windows are filtered by `group_id`

Instead, route rendering through a small resolver:

- embedded mode: use `doc.blueprint()` + `group_id`
- external mode: use external loaded blueprint + empty group/root scope

This affects:

- `visual::mutations::rebuild`
- `SubWindowRenderer::fitViewToContent`
- node-content rendering
- selection / hit testing / wire interaction

For external mode, editing should be disabled first.

### 3. Keep simulation export independent

Simulation/export path should continue to treat referenced blueprints as normal devices and let `parse_json_impl()` expand them.

This is separate from editor sub-window rendering.

Do not couple editor inspection windows to simulation flattening.

For parity:

- use the same composite-expansion semantics as the codegen/AOT pipeline
- do not add JIT-only bridge rewrites or editor-only expansion hacks that AOT cannot mirror

### 4. Allow arbitrary nesting naturally

Arbitrary nesting falls out if external windows recursively use the same rule:

- if clicked node is embedded nested group: open embedded sub-window
- if clicked node is external reference: load its blueprint and open external-reference window

No special-case depth handling should exist.

## Implementation Plan

### Phase 1: Loader Compatibility

Goal:

- allow library `.blueprint` files to load through bp2 codec without rejecting metadata used by the parser/type registry

Tasks:

- extend bp2 top-level allowed fields to tolerate library metadata
- extend bp2 interface allowed fields to tolerate `type` and `source_writer`
- keep bp2 decode strict on malformed values, but tolerant of extra library metadata it does not use

Acceptance:

- `WindowSystem::openDocument("library/math/FirstOrderLag.blueprint")` succeeds
- no `unknown top-level field: cpp_class`

### Phase 2: Window Source Abstraction

Goal:

- represent embedded and external windows differently

Tasks:

- extend `BlueprintWindow` with mode/source metadata
- add storage for externally loaded blueprint snapshot in the window or a dedicated lightweight view model
- keep root window behavior unchanged

Acceptance:

- window creation API can open:
  - root document window
  - embedded group window
  - external reference window

### Phase 3: External Reference Open Path

Goal:

- double-click / context menu on `expandable + blueprint_path` opens a readable external blueprint view

Tasks:

- change `Document::openSubWindow()` into a dispatcher:
  - `nested[]` -> embedded group window
  - `blueprint_path` -> external reference window
- resolve file path as `library/<blueprint_path>.blueprint`
- load referenced blueprint through existing persist/codec path
- mark external windows read-only

Acceptance:

- `closed_circuit.blueprint` can open `firstorderlag_1`
- title reflects source blueprint path
- no `nested 'firstorderlag_1' not found`

### Phase 4: Rendering Mode Split

Goal:

- render external referenced blueprints without requiring `group_id` content in the current document

Tasks:

- update sub-window renderer to choose blueprint source by window mode
- update `fitViewToContent()` to inspect the active blueprint source
- update scene rebuild path to use either:
  - document blueprint + group filter
  - external blueprint root graph
- keep editing disabled for external windows initially

Acceptance:

- external library blueprint renders correctly in sub-window
- root and embedded windows still behave the same

### Phase 5: Input and Inspector Integration

Goal:

- external windows should still be inspectable even if read-only

Tasks:

- make hit testing and selection operate on the active blueprint source for that window
- ensure inspector/property views understand read-only external windows
- block editing commands cleanly in external mode

Acceptance:

- can click nodes in `FirstOrderLag` window and inspect them
- cannot mutate referenced blueprint accidentally from parent document context

### Phase 6: Recursive External Nesting

Goal:

- support external blueprint references inside external blueprint windows too

Tasks:

- make open action recursive on window-local blueprint source
- ensure path resolution works relative to library root consistently
- avoid duplicate-open confusion by keying windows with source identity, not only group id

Suggested identity key:

- embedded: `doc_id + ":group:" + group_id`
- external: `doc_id + ":ext:" + blueprint_path`

Acceptance:

- external blueprint can itself contain referenced blueprints and open them
- repeated opens reuse the same window per source identity

## PR Breakdown

### PR 1: bp2 Codec Accepts Library Blueprint Metadata

Scope:

- `src/blueprint_v2/codec/blueprint_codec.cpp`
- tests for loading library blueprints through bp2 codec

Deliverables:

- accept `cpp_class`, `description`, `domains`, `scheduler_source`, `param_defaults`, `param_schema`, `solver_role`
- accept interface metadata fields needed by library files

Risk:

- low

### PR 2: External Reference Window Model

Scope:

- `BlueprintWindow`
- `WindowManager`
- sub-window creation APIs

Deliverables:

- explicit window mode/source metadata
- stable identity for embedded vs external windows

Risk:

- medium

### PR 3: Open Referenced Blueprint from `blueprint_path`

Scope:

- `Document::openSubWindow()`
- context-menu and double-click flows

Deliverables:

- `expandable + blueprint_path` opens external reference window
- `nested[]` keeps current embedded behavior

Risk:

- medium

### PR 4: Renderer Supports External Blueprint Source

Scope:

- `SubWindowRenderer`
- `visual::mutations::rebuild`
- view fitting and selection helpers

Deliverables:

- external blueprint rendered from loaded blueprint snapshot
- read-only interaction mode

Risk:

- medium-high

Parity note:

- renderer changes are editor-only, but must not change exported simulation structure

### PR 5: Recursive Nesting and Window Identity Cleanup

Scope:

- open-window reuse logic
- orphan cleanup logic
- nested external-open behavior

Deliverables:

- arbitrary nested opening works
- no accidental window reuse collisions

Risk:

- medium

Parity note:

- add nested reference regression coverage that exercises both JIT build/export and AOT flatten/codegen expectations

### PR 6: Editor UX and Regression Tests

Scope:

- tests
- polish labels / titles / logging

Deliverables:

- editor regression coverage for:
  - open referenced library blueprint from node
  - open embedded group
  - recursive external nesting
  - read-only enforcement in external windows
- AOT-JIT parity coverage for referenced/nested blueprint expansion

Risk:

- low

## Parity Checklist

Before declaring nesting work done, verify all of the following:

- JIT export expands the same referenced blueprint structure as AOT/codegen input
- exposed port routing is identical in JIT and AOT
- `BlueprintInput` / `BlueprintOutput` bridge semantics are identical in JIT and AOT
- nested external references flatten identically regardless of depth
- no editor-only fallback path changes simulation behavior

## Non-Goals

- do not auto-convert referenced nodes into `nested[]` during load
- do not embed/copy library blueprint contents into user documents just to make sub-windows work
- do not mix editor external-reference windows with simulation flattening/export concerns

## Recommended Immediate Next Step

Start with PR 1.

That unblocks direct loading of `library/math/FirstOrderLag.blueprint` and removes format friction before changing window/rendering architecture.
