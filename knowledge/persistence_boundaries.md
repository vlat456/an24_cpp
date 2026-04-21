# Persistence Boundaries

This document defines which repository files are authoritative for the active persistence path and which files are preserved only as library assets or legacy/reference material.

## Canonical Authority

The active canonical blueprint document format is defined only by:

- `knowledge/persistence_spec_v1.md`
- `src/blueprint_v2/codec/blueprint_codec.cpp`
- `src/blueprint_v2/library/library_index.cpp`

Canonical blueprint documents must use:

- `"format": "blueprint"`
- `"version": 1`

Canonical library index documents must use:

- `"format": "library_index"`
- `"version": 1`

No legacy aliases, fallback markers, or inferred formats are part of the canonical path.

## File Classes

### 1. Canonical Blueprint Documents

These are blueprint JSON documents meant to round-trip through the strict bp2 codec and editor/document persistence path.

Rules:

- they follow `knowledge/persistence_spec_v1.md`
- they do not carry session/workspace state
- they do not rely on removed fields or migration shims

Examples:

- strict bp2 persistence fixtures under `tests/blueprint_v2/`
- local editor document saves such as `blueprint.blueprint` or `GSC.blueprint` when present

### 2. Workspace / Session Persistence

These files are not canonical blueprint documents. They store editor session state only.

Examples:

- workspace/session persistence produced by `src/editor/visual/workspace_session_persist.cpp`

Rules:

- viewport/window/session state lives here, not in canonical blueprint documents
- these files are separate from blueprint authority

### 3. Library Assets

These are repository-authored type/library assets used by the library/type registry.

Examples:

- `library/**/*.blueprint`
- `library/library_index.json`

Rules:

- library `.blueprint` files are **v3 type-definition assets**, not v1 blueprint documents
- they carry `"version": "3.0"` with integer-encoded domains/directions
- they are consumed exclusively by `load_component_registry()` in `src/io/json/component_registry_json_loader.cpp`
- they must NOT be loaded through the strict v1 blueprint codec (`BlueprintCodec::decode()`)
- when the editor needs a composite library type as a `bp2::Blueprint` (e.g. for embedding or flattening), it uses `bp2::blueprint_from_type_definition()` which builds the blueprint from the in-memory type definition already parsed by `load_component_registry()`
- the library index (`library_index.json`) is authoritative for id-to-path resolution
- ad-hoc path guessing is not authoritative

### 4. Legacy / Reference Schematics

These files are preserved for reference, regression seeding, or later rewrite work. They are not canonical persistence authority.

Examples:

- `closed_circuit.blueprint`
- `t1.blueprint`
- `test_groundpower.blueprint`
- `test_groundpower_flat.blueprint`

Rules:

- active canonical persistence/editor tests must not rely on these files
- these files are handled only by explicit legacy/reference follow-up work
- they are not evidence that the canonical persistence cutover is complete

### 5. Curated Regression Fixtures

Some regression fixtures intentionally preserve older node/wire shapes for focused runtime checks. They are allowed only when explicitly isolated from canonical persistence claims.

Example:

- `tests/fixtures/closed_circuit_regression.blueprint`

Rules:

- use them only in narrowly scoped regression tests
- do not fall back from canonical tests to raw legacy schematics
- fail loudly if a required curated fixture is missing

## Active-Test Boundary

Active persistence/editor regression coverage must prove the strict current path, not legacy tolerance.

That means:

- bp2 codec and document tests use strict v1 blueprint documents
- local workspace-document checks verify `format: "blueprint"` and `version: 1`
- tests must not silently fall back to raw legacy schematics like `closed_circuit.blueprint`

## Non-Goals

This document does not rewrite or delete the legacy/reference files. Those follow-up actions are tracked separately.
