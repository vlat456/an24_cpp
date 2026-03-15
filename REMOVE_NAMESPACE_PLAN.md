# Remove `an24` Namespace — Implementation Plan

## Scope Summary

| Category | Count | Files |
|----------|-------|-------|
| `namespace an24 {` / `} // namespace an24` declarations | ~84 open + ~84 close | ~42 header + ~25 cpp files |
| `an24::` qualified references | ~444 | ~35 files |
| `using namespace an24;` directives | ~94 | ~60 files |
| `using an24::Symbol;` declarations | ~22 | 8 test files |
| `an24::v2` sub-namespace | ~28 | 6 files (v2/, persist.cpp, convert_library.cpp, 2 tests) |
| `generated/` files (codegen output) | ~22 | 6 files |
| Codegen string templates (emit `namespace an24`) | ~8 | `codegen.cpp` |
| Documentation / markdown | ~286 | ~8 .md files + AGENTS.md |

## Work Layers (ordered by dependency)

### Layer 1 — Core library (`src/jit_solver/`) — 12 files, ~180 replacements

The foundation. Everything else depends on these types compiling without `an24::`.

| Step | File(s) | Action |
|------|---------|--------|
| 1.1 | `state.h` | Remove `namespace an24 {` / `}` wrapper |
| 1.2 | `component.h` | Remove wrapper |
| 1.3 | `components/provider.h` | Remove wrapper |
| 1.4 | `components/port_registry.h` | Remove both wrapper blocks (lines 16-18 and 23-680) |
| 1.5 | `components/all.h` | Remove wrapper; delete all 89 `an24::SimulationState` → `SimulationState` |
| 1.6 | `components/all.cpp` | Remove wrapper; delete all 89 `an24::SimulationState` → `SimulationState` |
| 1.7 | `components/provider_components.h` | Remove wrapper |
| 1.8 | `components/explicit_instantiations.h` | Remove wrapper |
| 1.9 | `jit_solver.h` | Remove wrapper |
| 1.10 | `jit_solver.cpp` | Remove wrapper |
| 1.11 | `systems.h` | Remove wrapper |
| 1.12 | `scheduling.h` | Remove wrapper |
| 1.13 | `simulator.h` | Remove wrapper |
| 1.14 | `simulator.cpp` | Remove `using namespace an24;` |

### Layer 2 — JSON parser (`src/json_parser/`) — 2 files

| Step | File(s) | Action |
|------|---------|--------|
| 2.1 | `json_parser.h` | Remove wrapper |
| 2.2 | `json_parser.cpp` | Remove wrapper |

### Layer 3 — Codegen (`src/codegen/`) — 2 files, includes string template changes

| Step | File(s) | Action |
|------|---------|--------|
| 3.1 | `codegen.h` | Remove wrapper |
| 3.2 | `codegen.cpp` | Remove wrapper; remove the 8 string template lines that emit `namespace an24 {` and `} // namespace an24` into generated code |

### Layer 4 — V2 format (`src/v2/`) — 4 files

`an24::v2` becomes just `namespace v2 {`.

| Step | File(s) | Action |
|------|---------|--------|
| 4.1 | `blueprint_v2.h` | `namespace an24::v2 {` → `namespace v2 {`; update closing comment |
| 4.2 | `blueprint_v2.cpp` | Same |
| 4.3 | `convert.h` | Same |
| 4.4 | `convert.cpp` | Same |

### Layer 5 — Editor data layer (`src/editor/data/`) — 2 files

| Step | File(s) | Action |
|------|---------|--------|
| 5.1 | `node.h` | Remove the small `namespace an24 { ... }` block (lines 130-133) wrapping the enum hash specialization |
| 5.2 | `blueprint.h` | Remove `an24::` from `expand_type_definition` signature |
| 5.3 | `blueprint.cpp` | Remove `using namespace an24;` |

### Layer 6 — Editor visual layer (`src/editor/visual/`) — ~30 files

Remove `namespace an24 {` wrappers from all extracted classes, and `an24::` qualifiers from renderer/scene files.

| Step | File(s) | Action |
|------|---------|--------|
| 6.1 | All `an24` namespace wrapper files (popups, panels, tabs, layout, dialogs, menu, canvas, node types, windows, canvas_area, canvas_constants, node_content_renderer, canvas_renderer) | Remove `namespace an24 {` / `}` in `.h` and `.cpp` |
| 6.2 | `hittest.h` | Remove forward decl `namespace an24 { class VisualNodeCache; }` and the `using` alias |
| 6.3 | `spatial_grid.h`, `trigonometry.h`, `scene.h` | Remove `an24::` qualifiers |
| 6.4 | `node/node.h` | Remove `an24::PortType` → `PortType` |
| 6.5 | `node/node.cpp` | Remove `an24::node_utils::` → `node_utils::` |
| 6.6 | Renderer `.h`/`.cpp` files (wire_renderer, node_renderer, tooltip_detector, grid_renderer, blueprint_renderer) | Remove all `an24::` qualifiers and `using` aliases |
| 6.7 | `scene/persist.cpp` | Remove all `an24::v2::` → `v2::` (16 occurrences) |

### Layer 7 — Editor top-level (`src/editor/`) — 6 files

| Step | File(s) | Action |
|------|---------|--------|
| 7.1 | `document.h` | Remove `an24::` from `Simulator`, `JIT_Solver`, `TypeRegistry` |
| 7.2 | `document.cpp` | Remove `using namespace an24;` (2 occurrences) |
| 7.3 | `window_system.h` | Remove `an24::TypeRegistry` → `TypeRegistry` |
| 7.4 | `simulation.h` | Remove comment about `using namespace an24` |
| 7.5 | `simulation.cpp` | Remove `using namespace an24;` |
| 7.6 | `app.h` | Remove `an24::` qualifiers |
| 7.7 | `app.cpp` | Remove `using namespace an24;` (3 occurrences) |

### Layer 8 — Examples — ~7 files

| Step | File(s) | Action |
|------|---------|--------|
| 8.1 | All example `.cpp` files | Remove `using namespace an24;` |
| 8.2 | `convert_library.cpp` | Remove `an24::v2::` → `v2::` |

### Layer 9 — Generated files (`generated/`) — 6 files

| Step | File(s) | Action |
|------|---------|--------|
| 9.1 | All `.h` files | Remove `namespace an24 {` / `}` |
| 9.2 | All `.cpp` files | Remove `namespace an24 {` / `}` and `using namespace an24;` |

Note: these will be overwritten next time codegen runs, but Layer 3 ensures codegen no longer emits the namespace.

### Layer 10 — Tests — ~50 files, ~116 replacements

| Step | File(s) | Action |
|------|---------|--------|
| 10.1 | 8 test files with `using an24::Symbol;` | Delete those lines |
| 10.2 | ~40 test files with `using namespace an24;` | Delete those lines |
| 10.3 | `test_render.cpp` | Remove ~50 `an24::PortType` / `an24::Simulator` / `an24::TypeRegistry` etc. qualifiers |
| 10.4 | `test_persist.cpp` | Remove ~15 `an24::PortType` / `an24::TypeDefinition` qualifiers |
| 10.5 | `test_inspector.cpp` | Remove comment about `an24::Port` ambiguity — verify no actual ambiguity exists |
| 10.6 | `test_blueprint_v2.cpp`, `test_library_v2.cpp` | `using namespace an24::v2;` → `using namespace v2;` |

### Layer 11 — Documentation — ~8 files

| Step | File(s) | Action |
|------|---------|--------|
| 11.1 | `AGENTS.md` | Remove the line about `namespace an24 {}` and the closing comment convention |
| 11.2 | All other `.md` files | Update code examples to remove `an24::` / `namespace an24` |

## Build & Verify After Each Layer

After each layer, run:

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
cd build && ctest --output-on-failure
```

This ensures incremental correctness. If a layer introduces collisions (e.g., `Port` ambiguity between editor and jit_solver), fix immediately before proceeding.

## Known Risks

1. **`Port` name collision** — `test_inspector.cpp` has a comment explicitly noting ambiguity between `an24::Port` (from jit_solver's port_registry.h PortType enum) and the editor's `Port` struct. After namespace removal both are global. Need to check if both are ever included together — if so, rename one.

2. **`v2` becomes a top-level namespace** — after `an24::v2` → `v2`, the name `v2` is very generic. Acceptable since it's only used in a few files and internally.

3. **Generated files drift** — until codegen is re-run, the generated `.h`/`.cpp` files need manual edits. After Layer 3 fixes codegen, re-running it will produce clean output.

## Estimated Scope

- **~120 files** modified
- **~860 lines** changed (mostly deletions)
- **0 new files**
- Purely mechanical — no logic changes
