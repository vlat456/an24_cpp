JIT/AOT Electrical Solver — Architectural Review
ISSUE LIST
E-001 — Exceptions in the Electrical Solve Hot Path

---

## Umbrella: Persistence-Cutover Status & GitHub Issue Mapping (2026-04-11)

**Summary:** Repository-local persistence reset is complete. The canonical blueprint format (v1) is established, boundaries are explicit, legacy/reference schematics are accounted for, and the GitHub issue chain is closed.

### Status by GitHub Issue

| Issue | Title | Repo-Local Status | Evidence |
|-------|-------|-------------------|----------|
| **#99** | Remove legacy schematic fixtures from active persistence and editor regression paths | ✓ COMPLETE | Curated regression fixture (`tests/fixtures/closed_circuit_regression.blueprint`) is isolated; active tests do not fall back to raw legacy schematics; boundaries documented in `persistence_boundaries.md` §5 |
| **#100** | Rewrite or retire legacy reference schematics after canonical cutover boundary is enforced | ✓ INVENTORY COMPLETE | Explicit inventory in this file; all legacy files catalogued with disposition; `t1.blueprint`, `test_groundpower.blueprint`, `test_groundpower_flat.blueprint` deleted (marked `D` in git); `closed_circuit.blueprint` reserved for future rewrite; curated fixture is isolated |
| **#101** | Migrate repository-authored canonical blueprint documents and fixtures to strict v1 | ✓ IN REPO | `knowledge/persistence_spec_v1.md` defines canonical format; `src/blueprint_v2/codec/blueprint_codec.cpp` implements strict parser; strict v1 fixtures exist under `tests/blueprint_v2/` |
| **#102** | Define repository boundary between canonical blueprint documents and legacy reference schematics | ✓ DEFINED | `knowledge/persistence_boundaries.md` §4 defines legacy category rules; canonical authority is explicit in §1-3; no ambiguity remains |
| **#103** | Remove obsolete persistence naming and align docs/tests on final format markers | ✓ COMPLETE | Final format markers (`"format": "blueprint"`, `"version": 1`) are canonical; active docs/tests/specs are aligned; issue closed |

### Repo-Local Cutover Completeness

**What is done (blocking nothing):**
- Canonical blueprint spec is defined and strict (v1 only)
- Canonical codec is implemented and tested
- Legacy/reference schematics are inventoried and explicitly disposed
- Active persistence/editor paths use strict v1 with no raw legacy fallback in active test paths
- Boundaries between canonical, library, session, and legacy are documented

**What remains:**
- follow-up architectural/product work, if any, should be tracked as new issues outside the closed persistence-reset umbrella

### Active Path Authority

All active persistence code defers to:
- `knowledge/persistence_spec_v1.md` — canonical schema
- `knowledge/persistence_boundaries.md` — file role rules
- `src/blueprint_v2/codec/blueprint_codec.cpp` — strict parser
- Curated regression fixtures under `tests/blueprint_v2/` and `tests/fixtures/` (isolated from legacy)

### References

- Legacy inventory: This file, section "#100"
- Spec: `knowledge/persistence_spec_v1.md`
- Boundaries: `knowledge/persistence_boundaries.md`

---

## #100: Legacy/Reference Schematic Inventory & Disposition (2026-04-11)

**Status:** INVENTORY COMPLETED (Explicit disposition already in `persistence_boundaries.md`)

**Purpose:** Mechanical inventory and explicit classification of legacy/reference schematics as part of persistence-cutover work. See `knowledge/persistence_boundaries.md` for authoritative disposition rules.

### Inventory Summary

| File | Status | Role | Disposition | Evidence |
|------|--------|------|-------------|----------|
| `closed_circuit.blueprint` | Present | Legacy reference schematic (generator/starter circuit) | Preserved as-is; curated subset in `tests/fixtures/closed_circuit_regression.blueprint` | File exists; referenced in tests; no format/version v1 fields (legacy schema); see `persistence_boundaries.md` §4 |
| `t1.blueprint` | **Missing** | Listed in persistence policy but deleted | N/A | grep found no filesystem entry; only mentioned in `persistence_boundaries.md` as example of legacy category |
| `test_groundpower.blueprint` | **Missing** | Listed in persistence policy but deleted | N/A | grep found no filesystem entry; only mentioned in `persistence_boundaries.md` as example of legacy category |
| `test_groundpower_flat.blueprint` | **Missing** | Listed in persistence policy but deleted | N/A | grep found no filesystem entry; only mentioned in `persistence_boundaries.md` as example of legacy category |
| `tests/fixtures/closed_circuit_regression.blueprint` | Present | Curated regression fixture | Actively used; preserved in strict isolation | Referenced in `test_push_runtime_regression.cpp` and `test_external_ref_signal_mapping.cpp` |

### Explicit Disposition (from `persistence_boundaries.md`)

All legacy/reference schematics follow these rules (§4 of persistence_boundaries.md):

- **Active canonical persistence/editor tests must not rely on these files**
- **These files are handled only by explicit legacy/reference follow-up work**
- **They are not evidence that the canonical persistence cutover is complete**

The curated regression fixture (`tests/fixtures/closed_circuit_regression.blueprint`) is allowed only when explicitly isolated from canonical persistence claims (§5 of persistence_boundaries.md).

### Search Verification

**Grep for `closed_circuit.blueprint`:**
- Test references: `test_push_runtime_regression.cpp` (3 refs), `test_external_ref_signal_mapping.cpp` (3 refs)
- Documentation: `component_authoring.md` (reference design mention), `errors_TODO.md` (AZS behavior, component extraction notes)
- All active tests have **explicit fallback-to-legacy policy NO** comments

**Grep for `t1.blueprint`, `test_groundpower.blueprint`, `test_groundpower_flat.blueprint`:**
- Only mentioned in `persistence_boundaries.md` as examples of legacy category
- Zero references in tests, code, or active docs
- **Conclusion: Already deleted/removed from active codebase**

### Mechanical Facts

1. **`closed_circuit.blueprint` (117KB, modified 2026-04-11 08:02)**
   - Schema: Legacy (no `"format"` and `"version"` v1 fields at top level)
   - Used by: `test_push_runtime_regression.cpp`, `test_external_ref_signal_mapping.cpp`
   - Backup: `closed_circuit.blueprint.bak_2026-04-09_1619` (116KB, from 2026-04-09)

2. **`tests/fixtures/closed_circuit_regression.blueprint` (14KB, modified 2026-04-11 08:10)**
   - Role: Curated fixture (intentional node/wire shape preservation)
   - Active usage: Narrowly scoped regression tests in `test_push_runtime_regression.cpp`, `test_external_ref_signal_mapping.cpp`
   - **Properly isolated** from canonical persistence authority

3. **`t1.blueprint`, `test_groundpower.blueprint`, `test_groundpower_flat.blueprint`**
   - Status: **Deleted** (not present in filesystem, marked as `D` in `git status` — staged for deletion in pre-existing worktree)
   - Policy role: Preserved in documentation as historical legacy category reference
   - No follow-up action needed (already removed from active codebase)

### Conclusion

- Disposition for all legacy/reference schematics is **already explicit** in `persistence_boundaries.md`
- Active persistence cutover is **not blocked** by this inventory (canonical path is isolated)
- No rewrite, deletion, or promotion actions needed at this time
- Curated regression fixture is properly isolated and actively used

---

## JIT/AOT Parity Refactoring (2026-04)

**Summary:** Major refactoring to achieve JIT/AOT parity and remove architectural drift between runtime paths.

| Issue | Problem | Resolution |
|-------|---------|------------|
| #1 | JIT signal unions and runtime metadata | Refactored `signal_union_rules.h` — common rules for JIT and AOT |
| #2 | Dead code in `src/core/solvers/shared/` | Removed ODR hazard duplicate code |
| #3 | AOT codegen missing `update_dynamic_sources` | Added patch-logic generation |
| #4 | Signal count off-by-one (sentinel in wrong bucket) | Added sentinel to fixed_signals after deduplication |
| #5 | AOT not skipping `visual_only` devices | Added `skip_visual_only=true` to builder |
| #6 | AOT scheduler flat, no source/consumer ordering | Added bucket-based ordering |
| #7 | UnionFind duplication | Extracted to `src/core/utils/union_find.h` |
| #8 | Field name divergence (`element_id` vs `component_index`) | Unified in AOT codegen |
| #9 | Parity tests used JIT signal mapping | Rewrote tests with independent AOT allocation |
| #10 | TypeRegistry boilerplate duplication in tests | Extracted to `tests/test_fixtures.h` |
| #340 | Umbrella: eliminate special cases | CLOSED — all sub-issues done |
| #341 | P0: data-driven patch ops | CLOSED — PatchOpDecl in SolverRole |
| #342 | P1: unified build pipeline | CLOSED — DomainConfig template |
| #343 | P2: simulator step loop | CLOSED — NodalSlot + pointer-to-member |
| #344 | P3: delete classname_rules | CLOSED — data-driven dispatch |
| #345-#347 | AOT↔JIT dedup | CLOSED — shared build_algorithms.h, unified types |
| #348 | NODAL_DOMAIN_COUNT constant | CLOSED |
| #349 | JSON field name unification | CLOSED |
| #352 | Unify AOT/JIT extraction | CLOSED — ExtractionAdapter concept in element_extraction.h |

**Files created/modified:**

- `src/core/solvers/common/signal_union_rules.h` — unified signal union rules
- `src/core/utils/union_find.h` — extracted UnionFind
- `src/core/solvers/jit/build_signals.cpp`, `build_nodal_domain.cpp`, `simulator.cpp` — JIT changes
- `src/core/solvers/aot/codegen_source.cpp`, `codegen_header.cpp`, `codegen.h`, `electrical_codegen.cpp` — AOT changes
- `tests/test_aot_composite.cpp`, `tests/test_jit_aot_bridge_equivalence.cpp`, `tests/test_electrical_parity_fixtures.cpp`, `tests/test_fixtures.h` — new parity tests

**Testing:** 1463/1466 passed (3 pre-existing env-related failures)

---

### AZS Switch Behavioral Verification ✓ VERIFIED

**Status:** CLOSED

**Finding:** AZS (Anti-Zavarushka Switch) in `closed_circuit.blueprint` correctly triggers at ~130A when nominal is 80A. Verified behavior:
- AZS monitors current through protected circuit via current sense resistor
- When current exceeds threshold (130A), AZS opens the circuit
- Simulated behavior matches expected protective action

---

E-001 — Exceptions in the Electrical Solve Hot Path

- Severity: High
- Category: Performance
- Location: src/jit_solver/subsolvers/electrical_subsolver.cpp — 7 throw sites (lines 28, 151, 210, 216, 311, 324, 344)
- Problem: The solver uses throw std::runtime_error for both invariant violations and recoverable conditions (singular matrix). At line 282-288, the Gaussian solver uses try/catch as control flow — textbook exception misuse. Additionally, std::to_string() in the throw expressions allocates heap memory.
- Impact: Prevents the compiler from marking solve_electrical as noexcept, blocking optimizations across the entire call chain. On the singular-matrix fallback path (which fires routinely in the editor), exception machinery has real overhead.
- Fix: Make solve_dense_gaussian return bool. Use assert() for invariant violations in debug, early-return with sentinel in release. Remove all throws from the per-frame path. Mark solve_electrical as noexcept.

---

E-002 — Full Variant Scan for 5-10 Solver-Owned Components

- Severity: High
- Category: Performance
- Location: src/jit_solver/simulator.cpp:21-113 — update_dynamic_sources() and commit_solver_owned_devices()
- Problem: Both functions iterate ALL devices in br.devices (an unordered_map of ComponentVariant with 68+ types) doing std::visit on every entry, just to find the ~5-10 solver-owned components (CVS, AZS, Relay, Battery, etc.). This is O(N_devices × variant_jump_table) per frame — potentially 100+ variant visitations for ~10 actual matches.
- Impact: Branch mispredictions and cache pollution from visiting 60+ irrelevant component types every frame. This is pure waste.
- Fix: At build time, store typed pointers/references to solver-owned components in dedicated small vectors inside BuildResult. Then the per-frame functions become tight loops over known types — no variant visitation at all:
  struct SolverOwnedRefs {
  std::vector<std::pair<ControlledVoltageSource<JitProvider>_, ElectricalPrimitiveHandle_>> cvs;
  std::vector<std::pair<AZS<JitProvider>_, ElectricalPrimitiveHandle_>> azs;
  // etc.
  };

---

E-003 — JitProvider Uses Hash Map for Port Lookup

- Severity: Medium
- Category: Performance
- Location: JitProvider (port resolution uses unordered_map::find() per access)
- Problem: Every provider.get(PortNames::xxx) call in JIT mode does a hash lookup — hash computation, bucket traversal, pointer dereference. Components call this 2-8 times per execute(). At 144 Hz with 100+ components, that's 300-800+ hash lookups per frame.
- Impact: Measurable overhead compared to AOT (which resolves to compile-time constants). For a game targeting variable refresh rates, this adds up.
- Fix: Replace the hash map with a flat array indexed by PortNames enum value:
  struct JitProvider {
  uint32_t indices[static_cast<size_t>(PortNames::_COUNT)];
  uint32_t get(PortNames p) const { return indices[static_cast<size_t>(p)]; }
  };
  This brings JIT port access to a single array index — nearly matching AOT performance.

---

E-004 — float time\_ Precision Loss After ~2-4 Hours

- Severity: Medium
- Category: Correctness
- Location: src/jit*solver/simulator.h:57 — float time* = 0.0f;
- Problem: time* is accumulated via time* += dt every frame. At 60 Hz after ~2 hours (7200s), float ULP at that magnitude is ~0.001. At 144 Hz, dt ≈ 0.0069 which approaches the ULP — time effectively stops advancing. For MSFS2024, flights can easily last 2-5+ hours.
- Impact: Time-dependent behaviors (battery discharge, thermal accumulation, integrators) silently stop progressing during long flights.
- Fix: Change to double time\_ = 0.0;. Trivial one-line change. dt can stay float.

---

E-005 — ComponentVariant with 68+ Types

- Severity: Medium
- Category: Architecture / Performance
- Location: src/core/solvers/common/port_registry.h (ComponentVariant definition)
- Problem: ComponentVariant = std::variant<68 types>. Every std::visit instantiates 68 specializations, generating enormous jump tables. The variant itself is as large as the biggest component + alignment + discriminant. This bloats compile times and binary size significantly.
- Impact: Slow compilation (already noted in errors_TODO.md #5), binary bloat, and the per-frame variant scans in E-002 compound this cost. Each visit site generates a 68-entry branch table.
- Fix: Two options: (a) Split into domain-specific variants (ElectricalVariant, LogicalVariant, etc.) reducing each visit to ~20 alternatives, or (b) use type-erased wrappers with SBO for the device map, keeping std::variant only where exhaustive matching is needed.
- **Resolution:** DOCUMENTED — kept as-is by design. Per-frame dispatch already devirtualized (E-002). Variant only used as storage container. Architecture comments added.

---

E-006 — alignas(64) on std::vector Members Is Misleading

- Severity: Low
- Category: Correctness / Memory
- Location: src/jit_solver/state.h:17-21
- Problem: alignas(64) std::vector<float> values; aligns the vector control block (3 pointers on the stack/struct), NOT the heap-allocated data buffer. values.data() gets whatever alignment std::allocator provides (typically 16 bytes). Any future SIMD work would segfault or underperform.
- Impact: False confidence in alignment. Not currently causing bugs since no SIMD is used on these buffers, but it's a trap for future developers.
- Fix: Either use a custom aligned allocator, or remove the misleading alignas(64) to avoid confusion.
- **Resolution:** FIXED — removed misleading `alignas(64)`, added explanatory comment for future SIMD work.

---

E-007 — build_systems_dev() Is a ~2000-Line Monolith

- Severity: Medium
- Category: Architecture / Maintainability
- Location: src/jit_solver/jit_solver.cpp (entire file)
- Problem: Single function containing: union-find (~30 lines), ParamReader class (~70 lines), 80-way else if component factory (1200 lines), electrical island building (200 lines), scheduler wiring (~100 lines). Adding a new component requires finding the right spot in an 80-branch if-else chain.
- Impact: This is the #1 maintainability concern. Every new component touches this file. Merge conflicts are likely in team development.
- Fix: Extract into focused compilation units: component_factory.cpp (or use a self-registering registry pattern), signal_allocator.cpp, and keep island building in its existing subsolver module.

---

E-008 — Variable dt Without Sub-Stepping or Clamping

- Severity: Low-Medium
- Category: Correctness
- Location: Across the simulation — dt is passed directly from frame time
- Problem: At variable refresh rates, dt can spike (frame stutter, alt-tab, loading screen) to very large values. Components using Euler integration (value += rate \* dt) will produce discontinuities. Battery discharge, integrators, slew rate limiters, etc. all take unclamped dt.
- Impact: A single frame hitch at 1000ms dt could discharge a battery by 1000x normal, slam integrators to limits, or produce other non-physical jumps. For a game this manifests as flickering gauges or sudden state changes after a pause.
- Fix: Clamp dt to a maximum (e.g., dt = std::min(dt, 0.1f)) at the top of Simulator::step(). Optionally sub-step if dt > threshold. This is standard practice in game physics.
- **Resolution:** FIXED — `dt = std::min(dt, MAX_DT)` with `MAX_DT = 0.1f` at top of `Simulator::step()`.

---

E-009 — The 9-Phase Pipeline May Be Over-Engineered

- Severity: Low
- Category: Over-engineering
- Location: src/jit_solver/simulator.cpp — the step() function
- Problem: The 9-phase pipeline (passive stamp → first electrical → observers → logical 1 → control commit → actuator stamp + second electrical → logical 2 → sub-rate domains → finalize) solves a real ordering problem, but two electrical solves per frame is expensive. The second solve exists to handle actuator feedback (AZS/relay state changes) within the same frame.
- Impact: For a game where one-frame delay is acceptable, a single electrical solve with one-frame-delayed actuator states would halve the electrical solver cost.
- Fix: Consider whether the second electrical pass is worth it. If AZS/relay state changes can tolerate a one-frame delay (at 60Hz+ this is 16ms or less — invisible to players), you can collapse to a single solve pass and simplify the pipeline significantly.
- **Resolution:** ALREADY RESOLVED — the 9-phase pipeline was already collapsed to a single-solve 4-phase pipeline. Architecture comment added documenting the design decision.

---

OVERALL ASSESSMENT
Top 3 Things Done Well

1. The Provider pattern / JIT-AOT duality — Writing components once as templates over Provider and getting both interpreted (JIT) and zero-overhead compiled (AOT) execution is genuinely elegant. The AOT path folds port resolution to compile-time constants. This is a well-designed abstraction.
2. Electrical island partitioning with fast-path kernels — Solving independent electrical islands separately with specialized N=0, N=1, N=2 fast paths that skip the full Gaussian solver is smart. Most real aircraft circuits decompose into small islands, so the fast paths handle the common case.
3. Zero per-frame heap allocation in the solver — The scratch buffer design (ElectricalRuntimeState retaining capacity with reserve()) ensures no allocations in the hot path. The PushScheduler with type-erased function pointers is a clean devirtualization pattern.
   Top 5 Highest-Impact Improvements
   Issue
   E-001: Remove exceptions from solve*electrical
   E-002: Pre-build solver-owned component lists
   E-003: Replace JitProvider hash map with flat array
   E-004: float time* → double time\_
   E-008: Clamp dt to prevent physics explosions
   Design Decisions Worth Reconsidering

- Two electrical solves per frame (E-009): For a game at 60Hz+, one-frame-delayed actuator feedback is invisible. A single solve pass would halve the most expensive per-frame computation.
- 68-type std::variant (E-005): This is reaching the practical limits of variant-based polymorphism. Domain-split variants or type erasure would improve both compile times and runtime dispatch.
- Gaussian elimination for game physics: The solver is correct, but for "good enough" game physics, iterative methods (Gauss-Seidel) with 1-2 iterations might be sufficient for most islands. However, since the fast-path kernels (N=0/1/2) handle most cases and Gaussian is only used for N≥3 islands, this is actually fine in practice — the current design is well-tuned for the workload.
  ▣ Build · claude-opus-4.6 · 19m 13s
  Switch agent
  Compact session

# Architecture Issues & TODO

> Issues discovered during codebase analysis. Prioritize and address as needed.

### ~~13. Logical Phase Ordering Bug (Subtract/Splitter)~~ ✓ FIXED

**Commit:** this session

**Problem:** Logical components (Subtract, Add, etc.) ran only once, before the
actuator pass (Phase 4). When a logical node read a signal produced by a
`ControlledVoltageSource` (electrical_actuator, stamped in Phase 6), it saw 0V
instead of the converged voltage. This caused `Subtract.B = 0` in the GSC blueprint
when `sub.B` was wired through a Splitter to `cvs.v_pos`.

**Root cause:** The Splitter aliases all its ports (`i`, `o1`, `o2`) to one signal
via union-find. When this signal is also the CVS `v_pos` node, it participates in
the electrical passive pass (Phase 2) where parasitic conductance drains it to ~0.
The CVS only stamps its value in Phase 6 (actuator), but logical ran in Phase 4 —
too early to see the converged value.

**Fix:** Run the logical bucket **twice per step**:

- **Pass 1 (Phase 4):** Before actuator pass — feeds actuator `cmd` inputs
  (e.g., Multiply → CVS.cmd). Maintains zero-latency closed-loop control.
- **Pass 2 (Phase 7):** After actuator pass — reads converged actuator outputs
  (e.g., Subtract reads CVS.v_pos through Splitter). Fixes the Subtract bug.

**Pipeline (9 phases):**

1. Passive electrical stamp
2. First electrical pass
3. Electrical observers
4. **Logical pass 1** (feeds actuators)
5. Control commit
6. Actuator stamp + second electrical pass
7. **Logical pass 2** (reads converged actuator outputs)
8. Sub-rate domains (mechanical/hydraulic/thermal)
9. Finalize

**Files changed:**

- `src/jit_solver/simulator.cpp` — added dual logical passes
- `tests/test_port_map_regression.cpp` — updated `run_step()` helper
- `tests/test_and_gate_debug.cpp` — updated `run_step()` helper

**Regression test:** `PortMapRegression.Subtract_GSC_Topology_SignalIndices`

---

## Resolved

### ~~E-001 — Exceptions in the Electrical Solve Hot Path~~ ✓ FIXED

**Status:** CLOSED

**What was done:**

- Removed all 7 `throw std::runtime_error` sites from `electrical_subsolver.cpp`.
- Changed `solve_dense_gaussian` to return `[[nodiscard]] bool` (true=ok, false=singular).
- On singular matrix: preserves previous values (graceful fallback), increments `rt.counters.singular_fallbacks`.
- Duplicate fixed constraints on the same node: deduplicated silently instead of throwing.
- Invariant violations (e.g., unknown element kind) use `assert()` in debug, no-op in release.
- Marked `solve_electrical` as `noexcept`.

**Files changed:**

- `src/jit_solver/subsolvers/electrical_subsolver.cpp` — all throws removed, graceful fallback
- `src/jit_solver/subsolvers/electrical_subsolver.h` — `noexcept` added to declaration
- `tests/test_electrical_subsolver.cpp` — 3 EXPECT_THROW tests updated to graceful-fallback tests

**Regression tests:** `E001_Noexcept.SolveElectricalIsNoexcept`, `E001_Noexcept.SolveGaussianReturnsBool`, `E001_Noexcept.DuplicateFixedConstraintsSameValueNoThrow`

---

### ~~E-002 — Full Variant Scan for Solver-Owned Components~~ ✓ FIXED

**Status:** CLOSED

**What was done:**

- Added `SolverOwnedRefs` struct to `BuildResult` in `jit_solver.h` with 10 typed pointer vectors (batteries, generators, controlled_voltage_sources, variable_conductances, azs_switches, hold_buttons, relays, resistors, electrical_conductances, electrical_sources).
- Populated at end of `build_systems_dev()` in `jit_solver.cpp` with a single pass over the device map.
- Rewrote `update_dynamic_sources()` and `commit_solver_owned_devices()` in `simulator.cpp` to iterate pre-built typed pointer lists instead of full variant scan over all 68+ component types.

**Files changed:**

- `src/jit_solver/jit_solver.h` — `SolverOwnedRefs` struct, `solver_owned` field in `BuildResult`
- `src/jit_solver/jit_solver.cpp` — population loop at end of `build_systems_dev()`
- `src/jit_solver/simulator.cpp` — `update_dynamic_sources()` and `commit_solver_owned_devices()` rewritten

**Regression tests:** `E002_SolverOwnedRefs.PopulatedAfterBuild`, `E002_SolverOwnedRefs.PointersMatchDeviceMap`, `E002_SolverOwnedRefs.EmptyCircuitHasEmptyRefs`

---

### ~~E-003 — JitProvider Uses Hash Map for Port Lookup~~ ✓ FIXED

**Status:** CLOSED

**What was done:**

- Added `_COUNT` sentinel to `PortNames` enum in `port_names.h`.
- Updated codegen (`src/codegen/codegen.cpp`) to emit `_COUNT` sentinel.
- Replaced `JitProvider`'s `std::unordered_map<PortNames, uint32_t>` with flat array `uint32_t indices[static_cast<size_t>(PortNames::_COUNT)]`.
- Constructor memsets to `0xFF` (UNMAPPED = UINT32_MAX).
- Added `set()`, `get()`, `has()` methods. `get()` is a single array index — nearly matching AOT performance.
- `AotProvider` (compile-time constexpr fold expression) completely unaffected.

**Files changed:**

- `src/jit_solver/components/port_names.h` — `_COUNT` sentinel added
- `src/codegen/codegen.cpp` — codegen updated to emit `_COUNT`
- `src/jit_solver/components/provider.h` — `JitProvider` rewritten with flat array
- `tests/test_generator.cpp` — updated from `provider.indices[X] = Y` to `provider.set(X, Y)`
- `tests/test_electric_heater_regression.cpp` — same change

**Regression tests:** `E003_JitProviderFlatArray.DefaultConstructorAllUnmapped`, `SetAndGet`, `HasReturnsTrueOnlyForMapped`, `OverwriteMapping`, `SizeofIsSmall`, `PortNamesCountSentinel`, `AotProviderUnaffected`

---

### ~~E-004 — float time_ Precision Loss After ~2-4 Hours~~ ✓ FIXED

**Status:** CLOSED

**What was done:**

- Changed `float time_ = 0.0f` to `double time_ = 0.0` in `simulator.h`.
- Changed `get_time()` return type from `float` to `double`.
- Updated all `time_` literal references in `simulator.cpp` from `0.0f` to `0.0`.

**Files changed:**

- `src/jit_solver/simulator.h` — `float time_` → `double time_`, `get_time()` returns `double`
- `src/jit_solver/simulator.cpp` — `0.0f` → `0.0` for time_ references

**Regression tests:** `E004_DoubleTime.GetTimeReturnsDouble`, `E004_DoubleTime.PrecisionAfterManySteps`, `E004_DoubleTime.SimulatorTimeStartsAtZero`

---

### ~~E-005 — ComponentVariant with 68+ Types~~ ✓ DOCUMENTED (by design)

**Status:** CLOSED — no code change needed, architecture documented.

**What was done:**

- Added 30-line architecture comment block above `ComponentVariant` in `port_registry.h` explaining:
  - Why variant (not virtual): preserves template Provider pattern for AOT constexpr.
  - Per-frame dispatch is already devirtualized via `PushScheduler` (type-erased fn ptrs) and `SolverOwnedRefs` (E-002).
  - `std::visit` only used at build time (3 call sites in `jit_solver.cpp`), never in hot path.
  - Future direction: component count will shrink as wrappers decompose into primitives.
- Updated `BuildResult::devices` comment in `jit_solver.h`.

**Decision:** Keep `ComponentVariant` as-is. The variant is used only as a storage container — not for per-frame dispatch. Splitting would add complexity without measurable performance gain.

**Files changed:**

- `src/jit_solver/components/port_registry.h` — architecture comment block
- `src/jit_solver/jit_solver.h` — updated `BuildResult::devices` comment

**Regression tests:** `E005_ComponentVariant.IsStdVariant`, `E005_ComponentVariant.HasManyAlternatives`, `E005_ComponentVariant.ContainsBatteryAndAZS`

---

### ~~E-006 — alignas(64) on std::vector Members Is Misleading~~ ✓ FIXED

**Status:** CLOSED

**What was done:**

- Removed `alignas(64)` from all three `std::vector` members in `SimulationState` (`values`, `lut_keys`, `lut_values`).
- Added explanatory comment documenting why `alignas` on vector members is misleading (only aligns control block, not heap data) and what to do if SIMD alignment is needed in the future (custom aligned allocator).

**Files changed:**

- `src/jit_solver/state.h` — removed `alignas(64)`, added explanatory comment

**Regression tests:** `E006_NoMisleadingAlignas.SimulationStateDefaultAlignment`, `E006_NoMisleadingAlignas.VectorDataIsHeapAllocated`

---

### ~~E-008 — Variable dt Without Clamping~~ ✓ FIXED

**Status:** CLOSED

**What was done:**

- Added `dt = std::min(dt, MAX_DT)` at top of `Simulator::step()` with `MAX_DT = 0.1f`.
- Exposed as `static constexpr float MAX_DT = 0.1f` (public) in `Simulator` class for testability.
- Added detailed comment explaining why: frame hitches beyond 100ms are clamped to prevent physics explosions (battery over-discharge, integrator saturation, gauge flickering). Standard game physics practice.
- Pre-existing guards for `dt <= 0.0f` (early return) are preserved.

**Files changed:**

- `src/jit_solver/simulator.h` — `static constexpr float MAX_DT = 0.1f` added
- `src/jit_solver/simulator.cpp` — dt clamp at top of `step()`

**Regression tests:** `E008_DtClamp.MaxDtConstantExists`, `E008_DtClamp.LargeDtIsClamped`, `E008_DtClamp.NormalDtIsNotClamped`, `E008_DtClamp.ExactlyMaxDtIsNotClamped`, `E008_DtClamp.ZeroDtIsIgnored`, `E008_DtClamp.NegativeDtIsIgnored`

---

### ~~E-010 — Composite Port Lookup Failure in `get_port_value()`~~ ✓ FIXED

**Status:** CLOSED

**Problem:**

When a blueprint composite (e.g., `12SAM28`) is instantiated as device `sb`, the parser expands internal devices with colon prefix (`sb:src`, `sb:csense`, etc.) and BlueprintInput/Output bridge nodes become `sb:v_out` with ports `.ext` and `.port`. The `port_to_signal` map stores these as `sb:v_out.ext` (parent-facing) and `sb:v_out.port` (internal-facing), unified by union-find.

However, `get_port_value("sb", "v_out")` looked up `sb.v_out` (dot-separated) which doesn't exist in the map — the actual key is `sb:v_out.ext` (colon-separated). This caused all composite port lookups to return 0.

**Root cause:** Naming convention mismatch. Flat (non-composite) devices use `device.port` format, but expanded composites use `device:port.ext` format after parser rewrite.

**Fix:** Added fallback logic in `get_port_value()`: if the flat key `node_id.port_name` is not found in `port_to_signal`, try `node_id:port_name.ext` (composite bridge format). This is purely a query-side fix — no changes to the build pipeline or signal allocation.

**One-frame delay interaction:** Even with correct port lookup, composite feedback loops (LUT→CVS cmd) exhibit one-frame delay because CVS reads `cmd` in `update_dynamic_sources` (phase 1) but LUT writes `cmd` in `scheduler.step` (phase 3). Tests must account for 2-step warmup before measuring steady-state values.

**Files changed:**

- `src/jit_solver/simulator.cpp` — `get_port_value()` fallback to `node:port.ext`
- `tests/test_12sam28.cpp` — 2-step warmup before measurements

**Regression tests:** `SAM28Composite.InitialOutputsAreSane`, `SAM28Composite.DischargeDecreasesChargeAndSoc`, `SAM28Composite.SocToOcvFeedbackCausesVoltageDrop`

---

**Status:** CLOSED — the 9-phase pipeline described in the architectural review was ALREADY collapsed to a single-solve pipeline in the current codebase. No code change needed.

**Current pipeline (4 phases):**

1. `update_dynamic_sources` — stamp actuator states (AZS/Relay/CVS) from previous frame
2. `solve_electrical` — single Gaussian solve for all islands
3. `scheduler.step` — execute all logical/mechanical/etc. components
4. `commit_solver_owned_devices` — battery discharge, state transitions

**What was done:**

- Added 20-line pipeline architecture comment in `simulator.cpp::step()` documenting the single-solve design decision, one-frame delay semantics, and why the old 9-phase pipeline was collapsed.
- One-frame delay (16ms at 60Hz) for AZS/relay state changes is invisible to players and halves the electrical solver cost.

**Files changed:**

- `src/jit_solver/simulator.cpp` — pipeline architecture comment

**Regression tests:** `E009_SingleSolve.PipelineProducesConsistentElectricalResults`, `E009_SingleSolve.StepCountAndTimeConsistent`

---

### ~~DT-CONV — float dt → double dt Codebase Conversion~~ ✓ COMPLETED

**Status:** CLOSED

**What was done:**

Mechanical conversion of all `float dt` parameters and accumulator state variables to `double` precision throughout the simulation codebase. This prevents precision loss in time-integrated quantities during long simulation sessions (2+ hours).

**Scope of changes:**

1. **Core infrastructure:** `scheduler.h` (ExecuteFn/CommitFn typedefs), `simulator.h/.cpp` (step, MAX_DT), `electrical_subsolver.h/.cpp` (solve_electrical), `document.h/.cpp` (updateSimulationStep)
2. **Codegen:** `codegen.cpp` — all emitted `float dt` strings changed to `double dt` for AOT path
3. **All ~78 component headers:** execute/commit signatures changed to `double dt`
4. **All ~24 component .cpp files:** method definitions changed to `double dt`
5. **Accumulator variables promoted to double:** `accumulator.h` (state, next_state), `integrator.h` (accumulator, next_accumulator), `pid.h` (integral), `pi.h` (integral), `fuel_tank.h` (level, next_level), `azs.h` (temp), `ru19a.h` (timer, next_timer, current_rpm, next_current_rpm, t4, next_t4), `gs24.h` (wait_time, next_wait_time, current_rpm, next_current_rpm), `inertia_node.h` (rpm, next_rpm), `hydraulic_accumulator.h` (gas_volume), `battery.h` (capacity, charge)
6. **Type mismatch fixes:** Fixed `std::max`/`std::min`/`std::clamp` calls where promoted `double` accumulators or `double dt` were mixed with `float` literals (azs.cpp, fuel_tank.cpp, hydraulic_accumulator.cpp, ru19a.cpp, spring.cpp, pd.cpp, pid.cpp, pi.cpp, fast_tmo.cpp, asym_tmo.cpp, monostable.cpp, rug82.cpp)
7. **All ~45 test files + examples:** `float dt` variables and helper signatures updated

**Build:** Clean (0 errors, warnings only for unrelated nodiscard)
**Tests:** 1482/1482 passed, 0 failures

**Design notes:**

- `dt` parameter is `double` throughout the call chain but intermediate per-frame computations still use `float` where precision is not needed (voltages, currents, etc.). This is intentional — only time-accumulating variables need `double`.
- AOT codegen emits `double dt` signatures to match JIT.
- `static_cast<float>(dt)` used where `dt` feeds into float-precision expressions (e.g., `std::min(float_expr, 1.0f)`).

---

### 22. Extract Functional Blueprint Models to C++ Components

**Status:** OPEN

**Analysis (2026-04-02):**

Two blueprints in `closed_circuit.blueprint` contain reusable electrical engineering patterns that should be extracted to C++ components for optimization and reuse:

#### 22a. InductiveLoadModel

**Source:** `LoadVoltageDrp` blueprint (embedded definition lines 187-614)

**Purpose:** Models current through wires with R-L (resistance-inductance) characteristics:
- Computes: dI/dt = (V_ref - V_in - I*R) / L
- Integrates to get load current
- Used for voltage drop compensation in generator starter circuits

**Parameters:**
- `wire_r`: wire resistance (Ω) — 0.056Ω in example
- `inv_l`: inverse inductance (1/H) — 125 in example (= 1/0.008H)

**Ports:**
- Inputs: `v_in`, `v_ref`
- Output: `load_current`

**Proposed C++ component:** `InductiveLoadModel` in `src/jit_solver/components/inductive_load_model.h`

#### 22b. ThermalDeratingModel

**Source:** `DerateCtrl` blueprint (embedded definition lines 623-1107)

**Purpose:** Models I² heating and computes thermal derating factor:
- Integrates I² over time with thermal time constant
- Computes derated command based on overload capacity

**Parameters:**
- `i_cont_max`: continuous current max (A)
- `derate_time_constant`: thermal time constant (s)

**Ports:**
- Inputs: `current`, `command` (un-derated)
- Output: `derated_command` [0-1]

**Proposed C++ component:** `ThermalDeratingModel` in `src/jit_solver/components/thermal_derating_model.h`

**Why extract to C++:**
1. Both use Integrator internally — consolidating to single component eliminates redundant state
2. Common electrical engineering patterns — likely reusable in other aircraft systems
3. Fewer blueprint nodes to process at load time
4. Enables AOT optimization of the model computation

**Files to create:**
- `src/jit_solver/components/inductive_load_model.h`
- `src/jit_solver/components/inductive_load_model.cpp`
- `src/jit_solver/components/thermal_derating_model.h`
- `src/jit_solver/components/thermal_derating_model.cpp`
- `library/models/InductiveLoadModel.blueprint` (for blueprint registry)
- `library/models/ThermalDeratingModel.blueprint` (for blueprint registry)

**Acceptance criteria:**
- New components pass existing component test patterns
- `closed_circuit.blueprint` updated to use new components (or remain as-is for validation)
- Full test suite passes

---

### ~~23. Ref/Value Node Auto-Facing Ports + Free Placement~~ ✓ COMPLETED

**Status:** CLOSED

Ref/Value nodes (nodes with `render_hint == "ref"`) now have:
- Single port automatically faces toward the connected node (right/left/top/bottom edge)
- Dynamic size based on text content (no fixed dimensions, no grid snapping on size)
- Non-resizable (no resize handles, `isResizable() == false`)
- Value text rendered directly with proper vertical centering
- Half-grid snapping during drag (when only ref/value nodes are selected)
- Port edge placement is mathematically centered (no layout-grid rounding)

**Files changed:**

- `src/editor/visual/node/ref_node_widget.h` — `setPortLayoutSide()`, `port_layout_side_` member, removed `isResizable()`
- `src/editor/visual/node/ref_node_widget.cpp` — dynamic size from text, direct text rendering, no grid snapping, all 4 edge placements
- `src/editor/visual/scene_mutations.cpp` — `orient_ref_node_ports()` after scene rebuild
- `src/editor/input/canvas_input.h` — declared `orient_ref_node_port()`
- `src/editor/input/canvas_input.cpp` — `orient_ref_node_port()` + called from `commit_drag_node()`
- `src/editor/visual/snap.h` — dual-grid snap: `snap_axis()` + `snap_to_grid()` (whole-grid strong, half-grid weak, no type discrimination)
- `src/editor/visual/primitives/primitives.cpp` — Label estimate width 0.6→0.8

**Regression tests needed:**
- Ref/value node port orientation after scene rebuild
- Ref/value node port re-orientation after node drag
- Ref/value node port re-orientation for adjacent nodes when one is dragged
- Half-grid snapping during ref/value node drag
- Full-grid snapping when Shift is held during ref/value node drag
- Node size equals text width + 16px padding (no fixed 48×32)
- Text is vertically centered in node body
- Non-resizable (no resize handles drawn, `isResizable() == false`)

---

### ~~24. Bezier Wire Rendering (2-point)~~ ✓ COMPLETED

**Status:** CLOSED

Simple 2-point wires (no routing points) render as cubic Bezier curves:
- Control points: `c1 = start + (handle, 0)`, `c2 = end - (handle, 0)`
- `handle = clamp(|dx| * 0.45, 20, 140)` — adapts to wire length
- Arrowhead follows Bezier tangent
- Routed wires (with routing points) remain polyline (unchanged)

**Files changed:**

- `src/editor/visual/wire/wire.cpp` — `render_simple_bezier_wire()`, `eval_cubic_bezier()`, `draw_arrowhead_from_direction()`

**Note:** Hit testing and crossing detection still use straight-line polyline geometry.

---

### ~~25. Port Group Edge Centering~~ ✓ COMPLETED

**Status:** CLOSED

Multi-port nodes (standard component nodes) now center port groups on their respective edges:
- Left/right columns: Spacer children above and below port rows for vertical centering
- Top/bottom strips: no layout-grid X snapping (mathematically centered)
- Per-port vertical spacing preserved

**Files changed:**

- `src/editor/visual/node/visual_node.cpp` — body row layout with centered left/right port columns
- `src/editor/visual/container/port_row.h` — removed grid snapping from horizontal strip port X positions

---

### ~~29. InOut Port Direction on Non-Bus Nodes~~ ✓ FIXED

**Status:** CLOSED

**Rule:** `direction: "InOut"` ports are **only allowed on Bus nodes**. Non-Bus components must not use InOut direction.

**Problem:** In `library/systems/12SAM28.blueprint`, the CVS `src`'s `v_neg` port was declared as `"direction": "InOut"`. This violated the rule and caused editor port placement issues (InOut ports appear on both sides of the node visually).

**Fix (12SAM28):** Changed `v_neg` from `"direction": "InOut"` to `"direction": "In"`. The `v_neg` port is the target of wire `w_vin_src` (receives voltage from `v_in:port`), so `In` is the correct direction.

**Fix (library blueprint):** Also found that `library/electrical/ControlledVoltageSource.blueprint` had `v_neg` declared as `direction: 1` (Out), which placed it on the RIGHT side of newly inserted CVS nodes — wrong for a ground/reference terminal. Changed to `direction: 0` (In) so `v_neg` appears on the LEFT side where it belongs.

**Note:** The solver ignores port direction for CVS (uses `source_writer` metadata instead), so this is purely a visual/layout fix.

**Files changed:**

- `library/systems/12SAM28.blueprint` — `v_neg` direction: `InOut` → `In`
- `library/electrical/ControlledVoltageSource.blueprint` — `v_neg` direction: `Out` → `In`
- `src/jit_solver/components/port_registry.h` — regenerated via `update_port_registry`

---

### ~~26. path_to_node_port() Duplicated Across 5 Files~~ ✓ FIXED

**Status:** CLOSED

`path_to_node_port()` was copy-pasted in `canvas_input.cpp` and `scene_mutations.cpp`. Extracted to shared location.

**Files changed:**

- `src/editor/visual/snap.h` — added `editor_math::path_to_node_port()` inline function
- `src/editor/input/canvas_input.cpp` — uses `editor_math::path_to_node_port()`
- `src/editor/visual/scene_mutations.cpp` — uses `editor_math::path_to_node_port()`

---

### ~~27. O(n×m) Wire Scan in commit_drag_node()~~ ✓ FIXED

**Status:** CLOSED

Old `commit_drag_node()` scanned all blueprint wires once per moved node (O(nodes × wires)). Refactored to single-pass wire scan.

**Before:** O(moved_nodes × wires) — nested loops
**After:** O(wires + ref_nodes) — single wire scan builds map, then O(1) orient per ref node

**Changes:**

- `commit_drag_node()` — single wire scan, builds `ref_to_connected` map + `nodes_to_orient` set
- `orient_ref_node_port()` split into:
  - `orient_ref_node_port_impl(ref_id, connected_id)` — uses pre-built map (O(1))
  - `orient_ref_node_port_by_wire_scan(ref_id)` — fallback for disconnected refs

**Files changed:**

- `src/editor/input/canvas_input.h` — updated declarations
- `src/editor/input/canvas_input.cpp` — refactored commit + orientation logic

---

### ~~28. SAM28 LUT string_params Not Parsed in registry loader~~ ✓ FIXED

**Status:** CLOSED

The component-registry loader was reading `"params"` but ignoring `"string_params"` from v3 composite blueprints. LUT components inside 12SAM28 lost their lookup table data, outputting ~1V instead of ~25V.

**Root cause:** The v3 format stores string-valued parameters (like LUT `table`, Bus `port_edge`) in a separate `"string_params"` key. The registry loader only merged `"params"`.

**Fix:** Added `string_params` merge loop in the registry loader:
```cpp
if (n.contains("string_params") && n["string_params"].is_object()) {
    for (auto& [k, v] : n["string_params"].items()) {
        if (v.is_string()) dev.params[k] = v.get<std::string>();
    }
}
```

**Files changed:**

- `src/io/json/component_registry_json_loader.cpp` — `load_component_registry()` now merges `string_params`
- `tests/json_parser_test.cpp` — regression test `TypeRegistry.V3CompositeStringParamsMergedIntoDeviceParams`

**Tests:** All 1462 pass (was 1452 before fix)

---

### ~~30. Double-click right-click-inserted composite → Empty Window~~ ✓ FIXED

**Status:** CLOSED

Right-click-inserting a composite blueprint (e.g., 12SAM28 into `closed_circuit.blueprint`) created a sub-window that opened empty on double-click. The node was inserted and simulated correctly, but the sub-window had no content.

**Root cause (three-part):**

1. `addBlueprint()` created internal nodes only inside `nested.inline_def` (the nested blueprint's own node list) but did NOT add them to the root blueprint. The `rebuild()` function for sub-windows iterates `bp.nodes()` (root blueprint's node list) filtered by `group_id`. Since internal nodes were only in `nested.inline_def`, not in the root `nodes()` list, they were never rendered in the sub-window.

2. Even after promoting nodes to the root, wires were missing because they weren't added to the root blueprint, and node positions/wire routing points weren't preserved because `addBlueprint` was manually constructing the inline blueprint from `TypeDefinition` data (which lacks positions/routing_points).

3. **Wire object moved twice bug:** When remapping wires, `w_remapped` was moved into `remapped_bp` AND then moved again into `root_internal_wires`. Since `std::move` transfers ownership, the second `push_back(std::move(w_remapped))` received a moved-from object with empty fields. Both consumers need the same wire data — must copy before first move.

**Fix:** Refactored `addBlueprint()` to use `load_blueprint_from_file_validated()` — the same JSON parsing code used for loading saved documents. This gives us all metadata for free:
- Node positions from `"layout": {"x":..., "y":...}` in the `.blueprint` file
- Wire routing points from `"routing": []` in the `.blueprint` file  
- (Pre-#86: Viewport settings (`pan_x`, `pan_y`, `zoom`, `grid_step`) were once persisted, but canonical v1 no longer includes them)
- Library blueprint path lookup via `registry.categories[blueprint_name]` (e.g., `"systems"` → `library/systems/`)


After loading, all internal node IDs and wire endpoints are namespace-remapped to `unique_id + "_" + original_name` (e.g., `"12SAM28_1_battery"`) to avoid collisions when inserting the same blueprint multiple times. Remapped nodes and wires are added to the root blueprint via `cmd_add_node()` / `cmd_add_wire()`. The loaded blueprint (with remapped IDs) becomes `inline_def`.

Also fixed sub-window viewport: instead of blindly setting `pending_auto_fit = has_default_pan_zoom(*nested->inline_def)`, apply saved viewport from nested blueprint directly to window:

> **[PRE-#86 HISTORY]** The following code shows the old behavior before blueprint persistence was reset:
> ```cpp
> if (has_default_pan_zoom(*nested->inline_def)) {
>     win->pending_auto_fit = true;
> } else {
>     win->viewport.pan.x     = nested->inline_def->pan_x();
>     win->viewport.pan.y     = nested->inline_def->pan_y();
>     win->viewport.zoom      = nested->inline_def->zoom();
>     win->viewport.grid_step = nested->inline_def->grid_step();
>     win->viewport.clamp_zoom();
> }
> ```
> In canonical v1 persistence (post-#86), viewport state is no longer persisted in blueprints. Sub-windows now always auto-fit to content on first open.

**Key lesson:** Always reuse existing root-level document loading infrastructure. The root-level path already handles nodes, wires, positions, routing points, simulation integration, and oscilloscope rendering correctly. Don't manually reconstruct what JSON parsing already provides.


**Files changed:** `src/editor/document.cpp` — `addBlueprint()` function (complete rewrite), added `#include <filesystem>`, viewport fix in `openSubWindow()`

**Tests:** All 1462 pass

---

### ~~31. Indicator Content Not Rendered in Standard Layout~~ ✓ FIXED

**Status:** CLOSED

**Problem:** IndicatorLight component circle was never drawn in the editor. The component's simulation output (normalized brightness 0-1) was correct, but no visual circle appeared.

**Root cause:** In `visual_node.cpp::buildStandardLayout()`, the `NodeContentType::Indicator` case was **missing** from the content type switch. The standard layout path therefore failed to reserve the correct content region for indicator rendering, so the node fell through to the generic non-content layout path.

The `NodeContentType::Indicator` case WAS present in `buildFourSidedLayout()` (the override-based path), but IndicatorLight nodes have no layout overrides, so they always take the standard path where the case was missing.

**Fix:** Added `NodeContentType::Indicator` case to `buildStandardLayout()` so the node reserves the correct content geometry for indicator content.

**Lesson:** When adding a new content type, it must be registered in **both** layout paths:
1. `buildStandardLayout()` — standard layout (no port overrides) — the common path
2. `buildFourSidedLayout()` — four-sided layout (with port overrides)

Missing either one causes silent loss of the intended content region and therefore no visual output.

**Files changed:**
- `src/editor/visual/node/visual_node.cpp` — added Indicator case to `buildStandardLayout()`
- `tests/test_canvas_input.cpp` — semantic content rendering regressions
- `knowledge/05_editor.md` — documented semantic content pipeline
- `knowledge/errors_TODO.md` — this entry

**Regression tests:** semantic content rendering and hit-test coverage now lives under `tests/test_canvas_input.cpp`

---

### ~~Issue #133 — Remove Dual Authority Between Semantic Params and view.content_*~~ ✓ FIXED

**Status:** CLOSED

**Problem:** `view.content_*` fields served as a mutable shared cache written by two authorities:
1. `hydrate_node_view()` wrote ALL fields (static semantics + dynamic state) from semantic params
2. Simulation runtime overwrote dynamic fields (value, state, tripped) from port values

This created a fragile coupling where re-hydration (e.g., after inspector param edits) destroyed runtime state. The `properties_window.cpp` had an explicit hack (save/restore value/state/tripped around re-hydration) to work around this.

Additionally, `document_simulation.cpp` redundantly re-read min/max from semantic params (Voltmeter, Slider, KnobSwitch) even though those were already set by hydration — a second authority for the same data.

**Root cause:** No separation between static content semantics (type, label, min, max, unit) and dynamic runtime state (value, state, tripped). Both were written by `hydrate_node_view()` and both were overwritten by simulation.

**Fix:** Split `view.content_*` into two authority domains:

1. **Static semantics** (content_type, content_label, content_min, content_max, content_unit) — written ONLY by `hydrate_node_view()`. Single authority from semantic params + TypeDefinition.
2. **Dynamic runtime state** (content_value, content_state, content_tripped) — written ONLY by simulation/interaction. Never touched by `hydrate_node_view()`.

New function `initialize_node_content_defaults()` sets initial dynamic state at node creation/load time only.

**Changes:**
- `hydrate_node_view()` — no longer writes content_value, content_state, content_tripped
- New `initialize_node_content_defaults()` — sets initial dynamic state at creation time
- `hydrate_runtime_node_view_data()` — calls both functions at load time
- `document_components.cpp` — calls both at node creation time
- `properties_window.cpp` — removed save/restore hack (no longer needed)
- `document_simulation.cpp` — removed redundant re-reads of min/max/positions from semantic params

**Files changed:**
- `src/editor/blueprint_view_hydration.h` — split hydration into static + dynamic
- `src/editor/document_components.cpp` — added `initialize_node_content_defaults()` calls
- `src/editor/window/properties_window.cpp` — removed save/restore hack
- `src/editor/document_simulation.cpp` — removed redundant param re-reads
- `tests/blueprint_v2/test_codec.cpp` — updated existing tests, added 5 new #133 regression tests
- `tests/test_presentation_compiler.cpp` — added 5 new #133 regression tests

**Regression tests (10 total):**
- `Issue133_SingleAuthority.RehydrationPreservesRuntimeSliderValue`
- `Issue133_SingleAuthority.RehydrationPreservesRuntimeSwitchState`
- `Issue133_SingleAuthority.RehydrationPreservesTrippedState`
- `Issue133_SingleAuthority.RehydrationPreservesKnobPosition`
- `Issue133_SingleAuthority.FullHydrationSetsStaticAndDynamic`
- `Issue133_SingleAuthority.SliderUsesStaticMinMaxFromView`
- `Issue133_SingleAuthority.KnobUsesStaticMaxFromView`
- `Issue133_SingleAuthority.GaugeUsesStaticMinMaxAndUnit`
- `Issue133_SingleAuthority.ToggleUsesDynamicStateFromView`
- `Issue133_SingleAuthority.DynamicStateIndependentOfStaticSemantics`

**Tests:** All 1683 pass, 0 failures

---

### ~~15. Full Test Suite Migration to Push Runtime~~ ✓ COMPLETED (with explicit deprecations)

**Status:** Full OFF-mode suite builds and runs.

- `build_fulltests` with `-DPUSH_MIGRATION_TESTS_ONLY=OFF`: **1445/1445 passed**
- Push safety suite (`PushRuntime|PushBuildValidation|push_state|push_scheduler`): **35/35 passed**

#### Explicitly deprecated legacy solver-internal suites (disabled in `tests/CMakeLists.txt`)

These suites validate legacy solver internals or removed state arrays/stamping paths and are not meaningful in push architecture:

| Suite                                | Reason                                                |
| ------------------------------------ | ----------------------------------------------------- |
| `transformer_tests`                  | legacy iterative-era per-iteration/stamp assumptions  |
| `generator_tests`                    | legacy solver residual/stamp behavior                 |
| `apu_mechanical_tests`               | legacy iterative-era state-array coupling assumptions |
| `gs24_regression_tests`              | legacy iterative-era state-array coupling assumptions |
| `electric_heater_regression_tests`   | Removed `across/through/conductance` internals        |
| `switch_regression_tests`            | Removed legacy solver downstream passback internals   |
| `hydraulic_accumulator_regression_tests` | Removed legacy solver matrix/stamp assumptions        |
| `fuel_tank_regression_tests`         | Removed legacy solver matrix/stamp assumptions        |
| `refnode_regression_tests`           | Removed legacy solver residual assumptions            |
| `rug82_regression_tests`             | Removed legacy solver iteration semantics             |
| `solenoid_valve_regression_tests`    | Removed two-port legacy solver coupling semantics     |
| `legacy_regression_tests`            | Entirely legacy solver-specific by design             |

Push-era equivalents are covered by `push_runtime_regression_tests` and migration-specific validation suites.

---

### ~~12. Scheduler Refactor - Next Execution Plan~~ ✓ COMPLETED

**Commits:** `e3d2a9c`, `0df46d2`, `dbdf328`, `e9a079c`

**What was done:**

- Stabilized regression execution from `build/` harness.
- Migrated execution traits source-of-truth to blueprint metadata (`execution`).
- Unified JIT/AOT consumption of parsed execution metadata.
- Added strict parser/type-registry validation for missing/invalid execution metadata.
- Completed Stage-0 regression matrix including large-`dt` catch-up and minimal bridge JIT/AOT equivalence smoke.
- Synced scheduler epic checklist to implementation status.

---

### ~~11. Execution Traits Source-of-Truth in JSON~~ ✓ COMPLETED

**Commit:** `dbdf328`

**What was done:**

- Added execution metadata parsing/propagation on type definitions and merged devices.
- Removed class-name execution trait mapping from active scheduling/codegen paths.
- Updated library blueprint metadata and tests to metadata-driven execution traits.

---

### ~~1. Silent Out-of-Bounds in Release Builds~~ ✓ FIXED

**Commit:** `2cad5e8` (AotProvider sentinel), this session (JitProvider release log)

**What was done:**

- AotProvider now returns `UINT32_MAX` sentinel consistently (fold expression fix)
- JitProvider::get() now logs to `stderr` on the `[[unlikely]]` path in release builds
  instead of silently returning sentinel after compiled-out assert
- Component solve methods use `provider.has()` to guard optional ports

---

### ~~4. Magic Numbers in Domain Scheduling~~ ✓ FIXED

**Commit:** `1a0dcee`

All scheduling constants centralized in codegen under `DomainSchedule` namespace.
`systems.h` bucket array sizes now reference `DomainSchedule::*_PERIOD` instead of literals.

---

### ~~8. Memory Layout of SimulationState~~ ✓ FIXED

**Commit:** this session

**What was done:**

- Removed legacy fixed/dynamic SoA split fields from active JIT `SimulationState`.
- Active state now contains only `values`, LUT arenas, and transient `electrical_rt` pointer.
- Updated `allocate_signal()` to append directly into `values` without legacy bookkeeping.

---

### ~~16. Runtime API Simplification~~ ✓ COMPLETED

**Status:** CLOSED

**Decision:** Simplify component runtime API to minimum surface area.

**Final API (all 72 components):**

- `execute(SimulationState& st, double dt)` — main per-frame computation
- `commit(SimulationState& st, double dt)` — optional end-of-frame hook for stateful components (state machine transitions)
- `pre_load()` — initialization

**Removed from public API:**

- `commit_control()` — inlined into `commit()` for AZS, Switch, HoldButton, Relay
- `finalize_step()` — removed in earlier pass (no components had it)
- `solve_electrical/mechanical/hydraulic/thermal/logical()` — removed in earlier pass (no components had them)

**One-frame delay semantics:** Stateful components (Switch, Relay, HoldButton, AZS) apply staged transitions in `commit()`, meaning state changes take effect in the NEXT frame's `execute()`. This is the intentional push-model behavior.

**Files changed (final pass):**

- `src/jit_solver/components/azs.h` — removed `commit_control()` declaration
- `src/jit_solver/components/azs.cpp` — inlined `commit_control()` body into `commit()`
- `src/jit_solver/components/switch.h` — removed `commit_control()` declaration
- `src/jit_solver/components/switch.cpp` — inlined `commit_control()` body into `commit()`
- `src/jit_solver/components/hold_button.h` — removed `commit_control()` declaration
- `src/jit_solver/components/hold_button.cpp` — inlined `commit_control()` body into `commit()`
- `src/jit_solver/components/relay.h` — removed `commit_control()` declaration
- `src/jit_solver/components/relay.cpp` — inlined `commit_control()` body into `commit()`
- `src/codegen/codegen.cpp` — removed `commit_control` comment
- `tests/test_azs.cpp` — replaced all `commit_control()` calls with `commit()`
- `tests/test_push_runtime_regression.cpp` — replaced `commit_control()` calls with `commit()`

**Tests:** All 1422 pass

---

## High Priority

### ~~14. Zero-Fallback Metadata Cutover (MANDATORY)~~ ✓ COMPLETED

**Status:** CLOSED (2026-03-30)

**Policy:** Remove all fallback/compatibility behavior and make metadata fully explicit.

**Requirements:**

- No inference logic for execution, domains, or scheduling behavior.
- No legacy compatibility branches/shims for missing or stale metadata.
- No silent defaults for required fields.
- Invalid metadata must fail fast at load/build time.

**What was completed:**

- Removed compatibility whitelist in factory param validation (`known_library_unused_params`).
- Removed migrated-component gate path; unknown component classnames now hard-fail in factory build.
- Removed legacy `VoltageSense::observe_electrical()` shim API.
- Removed legacy simulator wire lookup port-name rewrite fallback (`:*.ext`).
- Normalized affected library blueprint `param_defaults` keys to align with strict consumption.
- Added strict negative regression tests for unknown class and rejected legacy/unknown params in push build validation suite.

**Target end-state:**

- `library/**/*.blueprint` metadata is fully normalized and explicit.
- `GSC.blueprint` and other working blueprints contain no stale/legacy fields.
- Validation runs strict-only mode with zero fallback.

**Result:**

- Build-time validation is strict-only for active push runtime paths.
- Invalid/legacy param usage now fails fast in build validation.

---

### ~~2. Dual Blueprint Systems (Incomplete Migration)~~ ✓ COMPLETED

**Status:** CLOSED (2026-03-30)

**Result:** Legacy editor blueprint/data files were removed from active codebase.

- Deleted legacy files: `src/editor/data/blueprint.h/.cpp`, `src/editor/data/node.h`,
  `src/editor/data/wire.h`, `src/editor/data/sub_blueprint_instance.h`.
- Removed dead legacy router headers: `src/editor/router/router.h`, `src/editor/router/grid.h`.
- Active code paths use `src/blueprint_v2/**` and shared `src/editor/data/node_content.h`.
- Build/test targets were updated to stop compiling legacy-dependent suites.

---

## Medium Priority

### ~~17. Replace String-Matched Output Port Classification With Metadata~~ ✓ DONE (via codegen)

**File:** ~~`src/core/solvers/jit/build_utils.cpp:output_ports_for_class()`~~

**Resolution (2026-04-23):**

Output port classification is now fully metadata-driven. `get_output_ports(ComponentKind)` in generated `port_registry.h` uses per-component `PORT_DIRECTIONS` arrays derived from blueprint port metadata. Source writer ports use `PORT_SOURCE_WRITER` arrays. Scheduler source classification uses `COMPONENT_SCHEDULER_SOURCE[]` indexed by ComponentKind. No hardcoded string lists remain.

**Impact:** Done. No string-based output port classification remains.

---

### ~~21. Strengthen Unknown-Class Fail-Fast in Metadata API~~ ✓ DONE (via codegen)

**Resolution (2026-04-23):**

Both `component_kind.h` and `port_registry.h` are now fully generated from the same codegen pass. The `is_scheduler_source_component()` and all other trait lookups do bounds-checked array access with `_COUNT` sentinel. Unknown classnames hit `parse_component_kind()` returning `nullopt`, which fails fast at elaboration time. Adding a new component requires only library blueprint + component header — no manual enum edits.

---

### ~~#217. Generate component_kind.h from codegen~~ ✓ COMPLETED

**Resolution (2026-04-23):**

`component_kind.h` is now auto-generated by `update_port_registry` alongside `port_registry.h` and `build_factory.cpp`. The codegen derives the enum, `parse_component_kind()`, `component_kind_classname()`, and `is_knob_switch_kind()` from library blueprint metadata. This eliminates the last manual sync point when adding new components — step 4 in the old workflow (manually adding enum entries) is gone.

**Files changed:**
- `src/core/solvers/aot/codegen.h` — `generate_component_kind()` declaration
- `src/core/solvers/aot/codegen_registry.cpp` — `generate_component_kind()` implementation
- `tools/update_port_registry.cpp` — calls `generate_component_kind()` first
- `src/core/model/component_kind.h` — now auto-generated (was hand-written)
- `knowledge/10_quick_reference.md` — updated workflow, generated file list

---

### ~~18. Remove ParamReader Forwarding Lambdas in `build_systems_dev()`~~ ✓ DONE (via codegen factory)

**File:** ~~`src/core/solvers/jit/build_components.cpp`~~ (deleted — replaced by codegen factory)

**Resolution (2026-04-23):**

The hand-written `build_components.cpp` and its 5 category split files have been replaced by the codegen factory (`build_factory.cpp`). The generated factory emits direct `param_reader.consume_*()` calls with no forwarding lambdas. This TODO is resolved.

**Impact:** Done. No forwarding lambdas remain.

---

### ~~19. Remove `MaxSelector -> Max` Metadata Alias Bridge~~ ✓ DONE

**File:** `src/jit_solver/jit_solver.cpp:metadata_classname_for()`

**Problem:**

- Runtime metadata lookup currently special-cases `MaxSelector` and maps it to `Max`.
- This is explicit (not inferred), but still creates coupling to a migration alias and hides schema drift risk.
- If `Max` metadata changes and `MaxSelector` diverges, behavior may silently differ from intent.

**Resolution (2026-03-31):**

- Deleted `src/jit_solver/components/max_selector.h` and `src/jit_solver/components/max_selector.cpp`.
- Removed `#include "max_selector.h"` from `all.h` and `jit_solver.cpp`.
- Removed `MaxSelector` if-branch from `metadata_classname_for()`.
- Changed `else if (dev.classname == "Max" || dev.classname == "MaxSelector")` to `else if (dev.classname == "Max")` in `build_systems_dev()`.
- Updated tests: removed `MaxSelector` special case in `make_device()` helpers, changed two test classnames from `"MaxSelector"` to `"Max"`.

**Impact:** Low-to-medium effort, improves strict-contract purity.

---

### ~~20. Unify Duplicate TypeDefinition Parse Paths~~ ✓ ADDRESSED

**Files:** `src/io/json/type_definition_json.cpp:parse_type_definition()`, `src/io/json/component_registry_json_loader.cpp:load_component_registry()`

**Problem:** Two separate parsers with different strictness — scheduler_source was missing from lenient path.

**Resolution:** The key regression (`scheduler_source` missing from `parse_type_definition`) is fixed and has regression tests:

- `test_push_build_validation.cpp` lines 740-763: scheduler_source regression
- `json_parser_test.cpp` lines 883-919: scheduler_source parity tests

Remaining differences are intentional — `parse_type_definition` is a lenient test-only path for legacy format (`classname`, `ports`, `params`), while `load_component_registry` is the strict production path for v3 format (`id`, `interface`, `param_defaults`). Full unification would introduce medium risk with minimal gain.

**Impact:** Resolved — no blocking drift risk with regression tests in place.

---

### 3. PORTS Macro Bloat — RESOLVED

**File:** `src/core/solvers/jit/component.h` — **DELETED**

The 221-line PORTS macro file was dead code. Zero components used it — all use
`Provider provider;` for runtime port lookup. The only consumer was a self-referential
test. Deleted entirely; test rewritten to validate port_registry.h codegen constants.

---

### 5. Deep ComponentVariant Compile Time

**File:** `src/jit_solver/jit_solver.h`

**Problem:** `ComponentVariant` has 80+ alternatives, causing:

- Slow compile times
- Binary bloat
- Long error messages

**Fix Options:**

- Group variants by domain: `ElectricalVariant`, `LogicalVariant`, etc.
- Use type erasure with small buffer optimization
- Consider `std::any` with custom RTTI

---

### ~~10. Sentinel Signal Ordering~~ ✓ FIXED

**File:** `src/jit_solver/jit_solver.cpp`

**Problem:** Sentinel ordering previously risked drifting from the fixed-signal set.

**Fix:** Added sentinel to `fixed_signals` after deduplication:

```cpp
result.fixed_signals.push_back(result.signal_count - 1);
```

**Regression test:** `PushBuildValidation.SentinelIsFixedSignal`
**Impact:** Low

---

## Low Priority

### 6. Alignment Without Runtime Verification

**File:** `src/jit_solver/state.h:20-32`

```cpp
alignas(64) std::vector<float> across;
alignas(64) std::vector<float> through;
// ...
```

**Problem:** `alignas` on vector member doesn't guarantee heap allocation alignment. The actual data pointer from `std::vector::data()` depends on allocator.

**Fix:**

- Use custom allocator: `std::vector<float, AlignedAllocator<64>>`
- Or allocate raw buffers: `std::unique_ptr<float[], AlignedDeleter>`

---

### 7. Potential Thread Safety Issues

**Files:** `src/blueprint_v2/` (EditorModel, TypeRegistry)

**Problem:** If editor loads blueprints in background threads while UI accesses them, could cause races.

**Status:** Unknown - needs investigation of actual threading model.

**TODO:** Audit thread usage in editor and add synchronization if needed.

---

### 9. Blueprint V2 Validation Coverage

**File:** `src/blueprint_v2/validation/`

**Questions:**

- Does it catch all cycles in nested blueprints?
- Does it validate domain compatibility on all wire endpoints?
- Are parameter types validated against component expectations?

**TODO:** Review validation coverage against known edge cases.

---

### ~~34. AZS VerticalToggle Content Click Area Too Narrow~~ ✓ FIXED

**Status:** CLOSED

**Problem:** Clicking on AZS (VerticalToggle) switches in the editor required pixel-perfect precision. The reserved semantic content area was only ~6.2px wide despite the node being 128px wide and the visible control rendering at 16px.

**Root cause:** In `buildVerticalToggleLayout()`, the center column containing the content region uses `setFlexGrow(1.0f)`. During `linearPreferredSize()`, flex children contribute 0 to the parent's preferred size. So the Row's preferred width = left_col + right_col (port label columns) only. After grid snapping to 128px, the center column got the remainder: `128 - 121.8 = 6.2px`.

The fix for this already existed in `NodeWidget::preferredSize()` conceptually — content preferred size must contribute back into node preferred width when the content region sits under a flex ancestor. But it was previously guarded too narrowly.

**Fix:** Replaced the `four_sided_layout_` flag with a generic check: if the content region sits under a flex ancestor (i.e., inside a flex-grow column), include its preferred size in the node's preferred width. This works for any content type, not just four-sided layouts.

- Removed `four_sided_layout_` member and its assignment
- Added generic parent-chain walk in `preferredSize()` to detect flex ancestor

**Files changed:**

- `src/editor/visual/node/visual_node.cpp` — generalized `preferredSize()` fix
- `src/editor/visual/node/visual_node.h` — removed `four_sided_layout_` member
- `tests/test_canvas_input.cpp` — regression test `VerticalToggleContentBoundsWideEnough`

**Regression tests:** `VerticalToggleContentBoundsWideEnough`, `ClickOnVerticalToggleContentReturnsToggle`

---

### ~~35. Content Click Detection Hardcoded to Specific Types~~ ✓ FIXED

**Status:** CLOSED

**Problem:** Content click detection in `CanvasInput` used hardcoded type checks and renderer knowledge to decide whether content was clickable or draggable. Adding a new interactive content type required editing the input path.

**Fix:** Replaced the type-specific path with semantic content hit objects and `InteractionBinding` metadata. `CanvasInput` now derives toggle/slider/knob behavior from semantic interaction kind instead of widget type knowledge.

**Files changed:**

- `src/editor/input/canvas_input.cpp` — semantic content hit-test and interaction dispatch
- `src/editor/visual/node/visual_node.cpp` — semantic content hit object generation
- `tests/test_canvas_input.cpp` — semantic interaction regressions

**Regression tests:** toggle, slider, knob semantic interaction regressions live in `tests/test_canvas_input.cpp`

---

### ~~35b. Presentation-Builder Content Snapshot Bridge Missed Root Placement Contract~~ ✓ FIXED

**Status:** CLOSED

**Problem:** After routing `NodeWidget` content snapshots through the shared presentation-layer snapshot builder, semantic content sessions became fragile and some test paths crashed or failed to enter toggle/slider/knob interaction. The synthetic content presentation tree did not fully satisfy the shared builder invariants.

**Root cause:** The synthetic content presentation built in `visual_node.cpp` initially omitted a root `FragmentPlacement` and mixed unrelated element ID spaces. The shared `build_semantic_scene_snapshot()` path expects every `PresentationNode` involved in content snapshot expansion to have matching placement data. At the same time, `CanvasInput` needed to treat `content_semantic_snapshot()` as node-local rather than world-space.

**Fix:**

1. `visual_node.cpp` now builds content snapshots through a self-contained synthetic presentation/layout pair with unique local element IDs and an explicit root placement.
2. `CanvasInput` now translates world pointer positions into node-local coordinates before semantic hit testing and before forwarding drag/release points into the semantic control session.
3. Added regression coverage for the root-placement invariant in the presentation snapshot tests.

**Files changed:**

- `src/editor/visual/node/visual_node.cpp` — synthetic content presentation builder with explicit root placement
- `src/editor/input/canvas_input.cpp` — local-space semantic hit test and initial dispatch
- `src/editor/input/canvas_input.h` — tracked active semantic widget id for session continuation
- `src/editor/input/canvas_input_mouse_drag.cpp` — local-space semantic drag continuation
- `src/editor/input/canvas_input_mouse_up.cpp` — local-space semantic release dispatch
- `tests/test_semantic_scene_snapshot.cpp` — root-placement regression coverage
- `tests/test_canvas_input.cpp` — local-space semantic snapshot expectations

**Regression tests:** `SemanticSceneSnapshotTest.ContentRootPlacementIsRequiredForNestedContentTrees` plus the semantic interaction/session regressions in `tests/test_canvas_input.cpp`

---

### ~~36. GroundPower Component — Tooltip Shows 0V~~ ✓ FIXED

**Status:** CLOSED

**Problem:** GroundPower (APR-2 ground power unit at 28.5V) was created as a composite blueprint. The simulation output was correct (downstream IndicatorLight showed 28.5V and brightness=1.0), but hovering over the `groundpower_1.v_out` port tooltip showed 0V.

**Root cause (two-part):**

1. **Simulation export** (`build_simulation_json()` in `document.cpp`): The embedded proxy's bridge nodes were generated with underscore convention (`groundpower_1_v_out`) by `addBlueprint()`, but the simulation JSON export assumed colon convention (`groundpower_1:v_out`). Parent-facing wires were not being rewritten to point to the actual bridge node ID.

2. **Signal key resolver** (`signal_key_resolver.cpp`): `resolve_runtime_signal_key()` for embedded composites assumed the bridge node ID was always `proxy_id:port_name.ext` (colon convention). When bridge nodes used underscore convention, the lookup returned empty string, so the tooltip could not find the runtime signal.

**Fix (two-part):**

1. **`document.cpp::build_simulation_json()`** — Added two-pass bridge node discovery: first pass scans by `group_id` + `name` (catches underscore-style nodes), second pass overrides with exact colon-style ID if present. Parent-facing wire endpoints now use the actual bridge node ID rather than assuming colon convention.

2. **`signal_key_resolver.cpp`** — Added `find_embedded_bridge_node()` function that tries colon convention first, then falls back to scanning nodes by `group_id` + `name` + type (`BlueprintInput`/`BlueprintOutput`). This handles both naming conventions.

3. **`tests/CMakeLists.txt`** — Added `blueprint_v2` library linkage to `signal_key_resolver_tests` and `external_ref_signal_mapping_tests` targets (needed because `signal_key_resolver.cpp` now uses `bp2::Blueprint::find_nested()` and `bp2::Blueprint::find_node()`).

**Files changed:**

- `src/editor/document.cpp` — `build_simulation_json()` two-pass bridge discovery
- `src/editor/signal_key_resolver.cpp` — `find_embedded_bridge_node()` with dual-convention support
- `tests/CMakeLists.txt` — `blueprint_v2` linkage for test targets

**Root architectural issue:** The codebase had two competing bridge-node naming conventions (colon `proxy:port` vs underscore `proxy_port`) created by different code paths. Fixed in issue #37 — colon convention is now canonical.

**Tests:** All 1422 pass

---

### ~~37. Bridge Node Naming Convention — Architectural Fragility~~ ✓ FIXED

**Status:** CLOSED

**Severity:** High (recurring bug source — caused E-010, #36, and simulation export bugs)

**Problem:**

The codebase had **two competing naming conventions** for bridge nodes (`BlueprintInput`/`BlueprintOutput`) when composites are flattened or materialized:

1. **Colon convention:** `proxy_id:port_name` — used by JSON parser expansion, `addComponent()` bridge insertion, runtime composite port lookups
2. **Underscore convention:** `proxy_id_port_name` — used by `addBlueprint()` node materialization

This caused at least 3 bugs and every new code path that touched composite boundaries needed fallback logic.

**Fix — unified on colon convention as canonical:**

1. **`document.cpp::addBlueprint()`** — Bridge nodes now use colon convention (`unique_id:original_name`). Internal (non-bridge) nodes continue to use underscore (`unique_id_original_name`). This aligns `addBlueprint()` with `addComponent()`, `src/io/json/parse_json_api.cpp`, and `simulator.cpp`.

2. **`document.cpp::build_simulation_json()`** — Removed two-pass bridge discovery workaround. Single-pass scan now works because all bridge nodes consistently use colon convention.

3. **`signal_key_resolver.cpp::find_embedded_bridge_node()`** — Removed structural fallback scan that searched by `group_id` + `name` + type. Now does direct ID lookup using colon convention only.

4. **`simulator.cpp::get_port_value()`** — Clarified comment from "Fallback" to "Composite" since the `node:port.ext` lookup is the standard composite path, not a fallback.

**Post-fix convention table:**

| Location | Convention | Fallback? |
|---|---|---|
| `src/io/json/parse_json_api.cpp` (composite expansion) | Colon | No |
| `document.cpp::addBlueprint()` (bridge nodes) | Colon | No |
| `document.cpp::addBlueprint()` (internal nodes) | Underscore | No |
| `document.cpp::addComponent()` (bridge creation) | Colon | No |
| `document.cpp::build_simulation_json()` | Colon only | No |
| `signal_key_resolver.cpp` | Colon only | No |
| `simulator.cpp::get_port_value()` | Colon only | No |
| `jit_solver.cpp::build_systems_dev()` | Agnostic (uses exact incoming names) | No |
| `blueprint_v2/flattener/` | Neutral (preserves existing IDs) | No |

**Files changed:**

- `src/editor/document.cpp` — `addBlueprint()` bridge node naming fix, `build_simulation_json()` simplification
- `src/editor/signal_key_resolver.cpp` — `find_embedded_bridge_node()` simplification
- `src/jit_solver/simulator.cpp` — `get_port_value()` comment clarification
- `knowledge/05_editor.md` — updated documentation

**Regression tests (5 new):**

- `SignalKeyResolver.EmbeddedBridgeNode_ColonConvention_Found`
- `SignalKeyResolver.EmbeddedBridgeNode_UnderscoreConvention_NotFound`
- `SignalKeyResolver.CompositePortKey_UsesColonConvention`
- `SignalKeyResolver.MultipleBridgeNodes_ResolveIndependently`
- `SignalKeyResolver.BridgeNode_ProxyIdWithUnderscores_ColonStillWorks`

**Tests:** All 1453 pass

---

## KnobSwitch Bugs (Issues 38-41)

### ~~38. Tick Marks Not Updating When Positions Changed in Inspector~~ ✓ FIXED

**Status:** CLOSED

**Problem:** When changing `positions` from 2→5 via the inspector, no new tick marks appear on the knob widget. The visual knob still shows only 2 tick marks.

**Root cause:** In `properties_window.cpp::apply()`, when user changes `positions` param via inspector, the bp2 node's `params` are updated but `content_max` is NOT synced. `rebuildAllWindows()` rebuilds widgets from stale `content_max`.

**Fix:** Added code in `properties_window.cpp::apply()` to sync `content_max`/`content_min` from params after param changes for Knob/Slider/Gauge content types.

**Files changed:**

- `src/editor/window/properties_window.cpp` — `apply()` now syncs content_max/min from params

**Regression tests:** 4 new tests in `test_properties_window.cpp`

**Tests:** All 1453 pass

---

### ~~39. InOut Terminals Duplicated on Both Sides~~ ✓ FIXED

**Status:** CLOSED

**Problem:** t1..t5 terminals are duplicated on both sides of the node (InOut port drawing limitation).

**Root cause:** In `visual_node.cpp::buildStandardLayout()`, the fast path pairs inputs[i] with outputs[i] by index. For InOut ports, the same port appears in BOTH arrays, causing duplicates on left AND right sides.

**Fix:** Modified `visual_node.cpp::buildStandardLayout()` to filter out InOut ports from the outputs list (they're already in inputs).

**Files changed:**

- `src/editor/visual/node/visual_node.cpp` — InOut filter in `buildStandardLayout()`

**Regression tests:** 2 new tests in `test_scene_mutations.cpp`

**Tests:** All 1453 pass

---

### ~~40. Node Editing Allowed During Simulation~~ ✓ FIXED

**Status:** CLOSED

**Problem:** Node dragging/resizing should be disabled during simulation mode, but knob drag should still work (currently clicking on knob drags the whole node). Also shouldn't be able to select nodes.

**Root cause:** `CanvasInput::read_only` is NOT tied to `simulation_running_`. During simulation, user can still drag nodes/resize them.

**Fix:** Added a new `simulation_mode` flag (separate from `read_only`) to allow widget interaction while blocking node editing:

- Blocks node dragging, selection, wire creation, resize, routing points, context menus, delete key
- Still allows slider/knob/toggle interaction and panning
- Also blocks node selection during simulation mode

**Files changed:**

- `src/editor/input/canvas_input.h` — Added `simulation_mode` flag
- `src/editor/input/canvas_input.cpp` — `simulation_mode` handling in `on_mouse_down`, `on_key`, `on_double_click`, right-click
- `src/editor/window/blueprint_window.h` — Added `set_simulation_mode()` method
- `src/editor/document.cpp` — wire up simulation start/stop to set simulation_mode

**Regression tests:** 8 new tests in `test_canvas_input.cpp`

**Tests:** All 1453 pass

---

### ~~41. initial_position Serialized as Float Instead of Int~~ ✓ FIXED

**Status:** CLOSED

**Problem:** Simulation fails to start for KnobSwitch because `initial_position` param is serialized as float string ("0.000000") but schema expects int ("0").

**Root cause:** In `document.cpp::build_simulation_json()`, all params are serialized with `std::to_string(float)` producing "0.000000", but `initial_position` and `positions` params are typed as `ParamSchemaType::Int` in the schema, causing validation failure.

**Fix:** Modified `document.cpp::build_simulation_json()` to check param schema type and serialize Int params as integer strings instead of float strings.

**Files changed:**

- `src/editor/document.cpp` — int param serialization in `build_simulation_json()`

**Tests:** All 1453 pass

---

### Port Direction Analysis (for Issue Discussion)

**Q: Do we need port direction information?**

No, but it's load-bearing in three places beyond the visual layer:

1. **Push-scheduler ordering** (`jit_solver.cpp:1288-1304`) — direction determines which ports are "writes" vs "reads" for topological sort. Without it, push components could execute in wrong order.

2. **Wire validation** (`path_resolver.cpp`, `canvas_input.cpp`) — prevents output-to-output and input-to-input connections.

3. **Blueprint persistence and codec** — direction is baked into the JSON format for both library definitions and saved blueprints.

The electrical subsolver doesn't care at all — it uses connectivity and solver roles, not direction.

**The pain points are specifically around InOut**, not direction in general. Options:

- Flatten InOut at the visual layer (treat as Input, never duplicate)
- Replace InOut with paired In/Out ports for electrical terminals

Removing direction entirely would touch ~50 source files, ~30 test files, and every `.blueprint` in the library.

---

## Summary Table

| #     | Issue                                      | Priority   | Effort | Status              |
| ----- | ------------------------------------------ | ---------- | ------ | ------------------- |
| 1-10  | JIT/AOT Parity Refactoring                | ~~High~~   | Medium | **FIXED**           |
| 1     | Silent OOB in release                      | ~~High~~   | Low    | **FIXED**           |
| 2     | Dual blueprint systems                     | ~~High~~   | High   | **FIXED**           |
| 3     | PORTS macro bloat                          | Medium     | Medium | Open                |
| 4     | Magic scheduling numbers                   | ~~Medium~~ | Low    | **FIXED**           |
| 5     | ComponentVariant compile time              | Medium     | High   | Open                |
| 6     | Alignment verification                     | Low        | Medium | Open                |
| 7     | Thread safety audit                        | Low        | High   | Open                |
| 8     | SimulationState invariants                 | ~~Low~~    | Low    | **FIXED** (partial) |
| 9     | Validation coverage                        | Low        | Medium | Open                |
| 10    | Sentinel signal ordering                   | Medium     | Low    | **NEW**             |
| 13    | Logical phase ordering (Subtract/Splitter) | ~~High~~   | Medium | **FIXED**           |
| E-001 | Exceptions in electrical solve hot path    | ~~High~~   | Low    | **FIXED**           |
| E-002 | Full variant scan for solver-owned devices | ~~High~~   | Medium | **FIXED**           |
| E-003 | JitProvider hash map for port lookup       | ~~Medium~~ | Low    | **FIXED**           |
| E-004 | float time_ precision loss                 | ~~Medium~~ | Low    | **FIXED**           |
| E-005 | ComponentVariant 68+ types                 | ~~Medium~~ | Low    | **DOCUMENTED**      |
| E-006 | Misleading alignas(64) on vectors          | ~~Low~~    | Low    | **FIXED**           |
| E-008 | Variable dt without clamping               | ~~Low-Med~~| Low    | **FIXED**           |
| E-009 | 9-phase pipeline over-engineering          | ~~Low~~    | Low    | **RESOLVED** (N/A)  |
| E-010 | Composite port lookup failure              | ~~High~~   | Low    | **FIXED**           |
| DT    | float dt → double dt conversion           | ~~Medium~~ | Medium | **COMPLETED**       |
| 22    | Extract functional blueprints to C++      | Medium     | Medium | **OPEN**            |
| 23    | Ref/Value node auto-facing ports + free placement | ~~Medium~~ | Medium | **FIXED**  |
| 24    | Bezier wire rendering (2-point)            | ~~Low~~    | Low    | **FIXED**           |
| 25    | Port group edge centering                  | ~~Low~~    | Low    | **FIXED**           |
| 26    | path_to_node_port() duplicated            | ~~Low~~    | Low    | **FIXED**           |
| 27    | O(n×m) wire scan in commit_drag_node     | ~~Low~~    | Low    | **FIXED**           |
| 28    | SAM28 LUT string_params not parsed        | ~~High~~   | Low    | **FIXED**           |
| 29    | InOut direction on non-Bus CVS port        | ~~Medium~~ | Low    | **FIXED**           |
| 30    | Double-click right-click-inserted composite shows empty window | ~~High~~ | Medium | **FIXED**   |
| 31    | Indicator content not rendered in standard layout | ~~High~~ | Low | **FIXED** |
| 32    | IndicatorLight brightness without ground | ~~High~~ | Low | **FIXED** |
| 33    | Indicator circle not centered in node | ~~Medium~~ | Low | **FIXED** |
| 34    | AZS VerticalToggle content click area too narrow | ~~High~~ | Low | **FIXED** |
| 35    | Content click detection hardcoded to specific types | ~~Medium~~ | Low | **FIXED** |
| 36    | GroundPower tooltip shows 0V (bridge naming) | ~~High~~ | Low | **FIXED** |
| 37    | Bridge node naming convention fragility | ~~High~~ | Medium | **FIXED** |
| 38    | KnobSwitch tick marks not updating in inspector | ~~High~~   | Low    | **FIXED** |
| 39    | KnobSwitch InOut terminals duplicated on both sides | ~~High~~   | Low    | **FIXED** |
| 40    | Node editing allowed during simulation | ~~High~~   | Medium | **FIXED**           |
| 41    | KnobSwitch initial_position serialized as float | ~~High~~   | Low    | **FIXED**          |
| 42    | Wire "energized" visualization uses voltage, not current | ~~Medium~~ | —      | **WONTFIX (by design)** |
| 43    | closed_circuit KnobSwitch kept legacy terminal names after strict rename | ~~High~~ | Low | **FIXED** |
| 44    | KnobSwitch family classname checks duplicated across builder paths | ~~Low~~ | Low | **FIXED** |

---

## ~~42. Wire "Energized" Visualization Uses Voltage Instead of Current~~ ⚠️ WONTFIX (by design)

**Status:** CLOSED — won't fix

**Rationale for closing:**

Wire visualization is **domain-agnostic** — wires carry all signal types (voltage, RPM, torque, bool, temperature, etc.). Making visualization "current-based" would tie it specifically to the electrical domain, breaking the generic design:

- `wire_is_energized(abs(voltage) > threshold)` is correct for electrical signals (0V at GND = no potential = not energized)
- RPM/torque/bool wires cannot be represented as "current" anyway
- "Energized = voltage present" is a domain-specific concept that doesn't generalize

**The user's confusion** is a UX issue: users interpret yellow = "current flowing", but the system shows yellow = "voltage potential present". This is a documentation/design issue, not a bug.

**Options for future UX improvement** (not bug fixes):
1. Add a UI toggle: "Show: Voltage / Current Flow" (per-viewport setting)
2. Document the current behavior explicitly in the editor UI
3. Add "current-based" as a separate visual layer on top of voltage-based (not a replacement)

---

## ~~43. closed_circuit KnobSwitch Still Used Legacy Terminal Names~~ ✓ FIXED

**Status:** CLOSED

**Problem:** After strict KnobSwitch terminal rename (`common`/`t*` -> `wiper`/`throw*`), `closed_circuit.blueprint` still had legacy endpoints for `knobswitch_1`. This made those wires silently non-connected (warning-only), resulting in a broken demo topology.

**Root cause:** Blueprint migration gap: library/schema/runtime were updated, but one real project blueprint kept old endpoint names.

**Fix:** Updated `closed_circuit.blueprint` KnobSwitch wiring to use strict port names (`wiper`, `throw1`).

**Regression tests:**
- `PushBuildValidation.KnobSwitchPortNamesAreWiperAndThrowsOnly`
- `PushBuildValidation.KnobSwitchLegacyPortNamesAreNotConnected`

**Notes:** Builder behavior for unknown ports remains warning + ignored connection (no throw). Tests now assert strict ports connect and legacy names do not.

---

## ~~44. KnobSwitch Family Classname Checks Were Duplicated~~ ✓ FIXED

**Status:** CLOSED

**Problem:** `KnobSwitch` / `RotarySwitch1ToN` / `RotarySwitchNTo1` classname checks were repeated in multiple `jit_solver.cpp` paths (`solver-owned` classification, component construction, electrical element extraction), increasing drift risk when adding family members.

**Fix:** Added `is_knob_switch_family(std::string_view)` helper in `jit_solver.cpp` and switched repeated checks to this helper.

**Additional hardening:** Builder now instantiates distinct variant alternatives for aliases (`RotarySwitch1ToN<JitProvider>`, `RotarySwitchNTo1<JitProvider>`) instead of always storing `KnobSwitch<JitProvider>`. This removes dead variant alternatives and validates alias type identity.

**Regression tests:**
- `PushBuildValidation.RotarySwitchAliasesInstantiateDistinctVariantTypes`
- Existing alias build/scheduler tests remain green
| 16    | Runtime API simplification (commit_control removal) | ~~Medium~~ | Low | **COMPLETED** |

---

## Issue #23 — Single-Source Composite Instances (COMPLETED)

**Problem:** Composite blueprint instances were stored in three redundant representations: (1) collapsed visual node, (2) nested blueprint record with `inline_def`, (3) promoted shadow copies of internal nodes/wires at root level with `group_id` filtering. This caused synchronization bugs, stale data, and forced repair code.

**Resolution (single-source model):**

1. **Removed root shadow promotion** from `addBlueprint()` — internal nodes/wires now exist only inside `nested.inline_def`

2. **Replaced `sync_bridge_to_collapsed_and_nested()`** with `add_bridge_port_to_composite()` — builds one `PortDescriptor`, applies to both collapsed node and nested record in a single mutation

3. **Subwindow rendering** now reads from `inline_def` directly:
   - `rebuildAllWindows()` and `rebuild_windows_after_history_change()` rebuild scenes from `nested.inline_def`
   - `BlueprintWindow` uses `EmbeddedInlineHost` to read/write `inline_def` through the root `EditorModel`
   - No shadow state or manual sync exists

4. **Simulation integration** updated for embedded nodes:
   - `updateNodeContentFromSimulation()` iterates both root nodes and `inline_def` nodes
   - `buildEnergizedWireSet()` handles embedded composite wires via `inline_def`
   - `triggerSwitch`/`setSliderValue`/`setKnobPosition`/`holdButton*` accept `group_id` to build prefixed simulation keys
   - `NodeContentRenderer::render()` uses `win.rendered_blueprint()` for correct node source
   - `fitViewToContent()` accounts for embedded scope (nodes have empty `group_id`)

5. **Export** uses `collect_nested_devices_recursive` / `collect_nested_connections_recursive` to walk `inline_def` trees

**Files changed:**
- `src/editor/document_components.cpp` — removed shadow promotion, renamed sync function
- `src/editor/document_simulation.cpp` — added `make_sim_id()`, `find_node_in_scope()` helpers; fixed all interaction functions
- `src/editor/document_history.cpp` — undo/redo with `inline_def` sync
- `src/editor/document_input.cpp` — `applyInputResult()` dispatches input results
- `src/editor/document_export.cpp` — recursive inline_def traversal
- `src/editor/window/blueprint_window.h` — `BlueprintWindow` with `EditingHost`, no shadow state
- `src/editor/visual/node/visual_node.cpp` — semantic node content render path reads from `rendered_blueprint()`
- `src/editor/visual/windows/sub_window_renderer.cpp` — fit-to-content fix
- `knowledge/05_editor.md` — updated documentation

**Tests:** All 1427 pass, including 3 new test files:
- `test_issue_23_nested_inline_only.cpp` — no shadow nodes, persistence roundtrip, nested-of-nested
- `test_embedded_editing_undo.cpp` — undo/redo of inline_def edits
- `test_embedded_subwindow_scene.cpp` — scene rebuild from inline_def

---

## Open Issues (2026-04)

| Issue | Title | Priority | Status |
|-------|-------|----------|--------|
| #350 | Remove JIT dependency from common/nodal_patch_ops.h | Low | Open |
| #351 | Migrate JIT callers from build_common:: to build_algo:: namespace directly | Low | Open |
| #353 | Fix O(n²) element lookup in AOT build_device_bindings | Medium | Open |
| #354 | DRY: extract shared map-building helper for handle assignment | Medium | Open |

### #352 — Unify AOT/JIT Extraction ✅ CLOSED

**Commit:** `cee58e57`

**Problem:** `extract_solver_role_element()` in AOT was a complete copy of JIT electrical extractors. Different error handling (JIT throws, AOT defaults). Adding a new SolverRoleKind required editing both files.

**Resolution:** Introduced `ExtractionAdapter` concept in `element_extraction.h`. Both JIT and AOT provide their own adapter:
- `JitExtractionAdapter` — strict (throws on missing data)
- `AotExtractionAdapter` — lenient (returns defaults, skips missing ports)
- Both share identical extraction logic via `extract_with_table()` dispatch

**Deleted:** 4 hand-written JIT electrical extractors, 3 JIT pressure extractor templates, `ElementExtractor`/`ExtractorFn`/`find_extractor` infrastructure, old AOT if-chain.

**Net:** -207 lines (136 added, 343 removed). 1890/1890 tests pass.
