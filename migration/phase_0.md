# Phase 0: Ground Rules and Overview

Historical note: this phase document predates the parser/registry cleanup. References below to legacy `json_parser` paths or APIs are historical and do not describe the current architecture.

## What This Migration Is

We are replacing the current blueprint persistence system (3 representations, string-based scoping, fragile bake-in) with a single canonical `Blueprint` type that handles everything. See `BLUEPRINT_ARCHITECTURE_V2.md` for the full design.

## Directory Convention

All new code lives under `src/blueprint_v2/`. Each sub-feature gets its own directory:

```
src/blueprint_v2/
    path/           <- Phase 1: Path, PathArena
    interface/      <- Phase 2: PortDescriptor, Interface, Direction
    blueprint/      <- Phase 3: Blueprint canonical type
    registry/       <- Phase 4: TypeRegistry
    codec/          <- Phase 5: BlueprintCodec (JSON serialization)
    flattener/      <- Phase 6: Flattener (hierarchy -> flat netlist)
    editor_model/   <- Phase 7: EditorModel (undo/redo, indices, bridge)
```

Tests live under `tests/blueprint_v2/` mirroring the same structure:

```
tests/blueprint_v2/
    test_path.cpp
    test_interface.cpp
    test_blueprint.cpp
    test_registry.cpp
    test_codec.cpp
    test_flattener.cpp
    test_editor_model.cpp
    test_bridge.cpp
```

## Hard Rules For The Agent

1. **TDD. No exceptions.** Every line of production code must be preceded by a failing test. The workflow for every item is:
   - Write the test. It must compile. It must fail (RED).
   - Write the minimum code to make the test pass (GREEN).
   - Refactor if needed. All tests must still pass (REFACTOR).
   - Move to the next test.

2. **Function size limit: 60 lines max.** If a function exceeds 60 lines, split it into helper functions. No exceptions. Count only non-blank, non-comment lines.

3. **File organization.** Every `.h` file has a matching `.cpp` file if it contains non-trivial logic. Header-only is allowed only for trivially small types (under 30 lines of logic). Each file belongs in its feature directory.

4. **No code scattering.** If a type belongs to `src/blueprint_v2/path/`, all its implementation lives there. Never put a function that belongs to `Path` into `blueprint.cpp` or any other unrelated file.

5. **No legacy support.** We do NOT maintain backwards compatibility with old tests. If an existing test breaks because of this refactor, it means it was testing old code that we are replacing. Either **rewrite** the test to use `bp2::` types, or **delete** it. Do not waste time fixing old tests to keep them limping along -- eliminate or rewrite. After every step, run `cd build && ctest --output-on-failure` and verify that all *non-deleted* tests pass.

6. **Namespace.** All new code lives in `namespace bp2 { }`. This avoids collisions with the existing `ui::` namespace and the current `Blueprint` class.

7. **No touching old code yet.** Phases 1-6 are purely additive. You are building a parallel system. The old code in `src/editor/data/` is not modified until Phase 7 (bridge) and Phase 8 (removal).

8. **Use existing InternedId.** The existing `ui::InternedId` and `ui::StringInterner` in `src/ui/core/interned_id.h` are good enough. Do NOT rewrite them. The new `bp2::` types will use `ui::InternedId` directly. A shared `ui::StringInterner` instance is passed around explicitly (never a global/static).

9. **Use existing Domain enum.** The existing `Domain` enum in `src/json_parser/json_parser.h` is fine. Use it as-is. Do NOT duplicate it or move it in phases 1-6.

10. **CMake.** Add a new static library target `blueprint_v2` in `src/blueprint_v2/CMakeLists.txt`. Wire it into `src/CMakeLists.txt` via `add_subdirectory(blueprint_v2)`. Tests get their own section in `tests/CMakeLists.txt`.

## Build/Test Commands

```bash
# Configure (first time or after CMakeLists changes)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build everything
cmake --build build -j$(sysctl -n hw.ncpu)

# Run ALL tests (must all pass after every step)
cd build && ctest --output-on-failure

# Run only blueprint_v2 tests (for fast iteration)
cd build && ctest -R "bp2_" --output-on-failure
```

## Phase Dependency Chain

```
Phase 1: Path, PathArena           (no dependencies)
Phase 2: Interface, PortDescriptor (depends on Phase 1 for InternedId only)
Phase 3: Blueprint canonical type  (depends on Phase 1 + Phase 2)
Phase 4: TypeRegistry              (depends on Phase 2 + Phase 3)
Phase 5: BlueprintCodec            (depends on Phase 3 + Phase 4)
Phase 6: Flattener                 (depends on Phase 3 + Phase 4 + Phase 5)
Phase 7: EditorModel + Bridge      (depends on all above + old code)
Phase 8: Cleanup                   (remove old code, redirect all users)
```

## Domain Note: Logical Components on Electrical Layer

Some components declared `Domain::Logical` (e.g., `Add`, `Multiply`, `LUT`) share the `st.across[]` signal array with `Domain::Electrical` components. This is by design -- Logical components run after SOR convergence and write directly to `across[]`. Some components in `library/logical/` (PID, PD, PI, P) are actually declared `Domain::Electrical` and stamp a tiny `1e-6f` conductance to keep MNA well-conditioned. This cross-domain interplay is NOT something we change in this migration. It is an existing design decision that works correctly. The migration only replaces the blueprint persistence/hierarchy layer, not the solver dispatch.

## Verification Checklist (run after every phase)

- [ ] `cmake --build build -j$(sysctl -n hw.ncpu)` succeeds with zero errors
- [ ] `cd build && ctest --output-on-failure` -- ALL tests pass (old + new)
- [ ] No function exceeds 60 lines
- [ ] All new files are in `src/blueprint_v2/<feature>/` or `tests/blueprint_v2/`
- [ ] All new code is in `namespace bp2`
Historical document note: this phase predates the #166 architecture cleanup. References to `src/json_parser/*`, `json_parser`, and `load_type_registry()` are historical and do not describe the current architecture.
