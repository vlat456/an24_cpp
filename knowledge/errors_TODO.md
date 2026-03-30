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

### ~~15. Full Test Suite Migration to Push Runtime~~ ✓ COMPLETED (with explicit deprecations)

**Status:** Full OFF-mode suite builds and runs.

- `build_fulltests` with `-DPUSH_MIGRATION_TESTS_ONLY=OFF`: **1445/1445 passed**
- Push safety suite (`PushRuntime|PushBuildValidation|push_state|push_scheduler`): **35/35 passed**

#### Explicitly deprecated legacy solver-internal suites (disabled in `tests/CMakeLists.txt`)

These suites validate legacy solver internals or removed state arrays/stamping paths and are not meaningful in push architecture:

| Suite | Reason |
|---|---|
| `transformer_tests` | legacy iterative-era per-iteration/stamp assumptions |
| `generator_tests` | legacy solver residual/stamp behavior |
| `apu_mechanical_tests` | legacy iterative-era state-array coupling assumptions |
| `gs24_regression_tests` | legacy iterative-era state-array coupling assumptions |
| `electric_heater_regression_tests` | Removed `across/through/conductance` internals |
| `switch_regression_tests` | Removed legacy solver downstream passback internals |
| `gidro_accumulator_regression_tests` | Removed legacy solver matrix/stamp assumptions |
| `fuel_tank_regression_tests` | Removed legacy solver matrix/stamp assumptions |
| `refnode_regression_tests` | Removed legacy solver residual assumptions |
| `rug82_regression_tests` | Removed legacy solver iteration semantics |
| `solenoid_valve_regression_tests` | Removed two-port legacy solver coupling semantics |
| `legacy_regression_tests` | Entirely legacy solver-specific by design |

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

### ~~8. Memory Layout of SimulationState~~ ✓ FIXED (partial)
**Commit:** this session

**What was done:**
- Added debug asserts in `allocate_signal()`: SoA size consistency, `dynamic_signals_count <= across.size()`
- Added debug asserts in `precompute_inv_conductance()`: bounds and size mismatch checks
- Convergence methods already had `std::min` guards (from `2cad5e8`)

**Remaining:** Sentinel signal is allocated as dynamic but sits after fixed signals,
causing `dynamic_signals_count` to include the fixed signal range. This is functionally
harmless (fixed signals get parasitic G which doesn't change their value) but is
architecturally imprecise. Consider making the sentinel a fixed signal.

---

### 16. Runtime API Simplification (ONGOING)
**Status:** OPEN (Phase 1 - commit hook introduced)

**Decision:** Simplify component runtime API to minimum surface area.

**Keep:**
- `execute(st, dt)` — main per-frame computation for stateless components
- `commit(st)` — optional end-of-frame hook for stateful components (state machine transitions)
- `pre_load()` — initialization

**Domain taxonomy (`solve_electrical`, `solve_mechanical`, `solve_hydraulic`, `solve_thermal`, `solve_logical`) is legacy** and will be removed from public API. Components may keep these as private helpers temporarily during migration.

**State transitions:** `finalize_step` and `commit_control` are unified into `commit()`. Incremental migration:
- Components with `commit_control` → call it from `commit()`
- Components with `finalize_step` → call it from `commit()`
- No behavioral change in this slice (existing methods stay)

**One-frame delay semantics:** Stateful components (Switch, Relay, HoldButton, AZS) apply staged transitions in `commit()`, meaning state changes take effect in the NEXT frame's `execute()`. This is the intentional push-model behavior.

**Files touched:** `src/jit_solver/scheduler.h`, 7 stateful component headers/implementations.

**Planned quality pass:**
- After current execute/commit migration checkpoint commit, run `@escalate` for a dedicated **10/10 code-quality plan** based on latest strict review findings.
- Deliverable should include: prioritized tasks, risk ranking, and verification gates.

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

### 2. Dual Blueprint Systems (Incomplete Migration)
**Files:** 
- `src/editor/data/blueprint.h` (legacy)
- `src/blueprint_v2/blueprint/blueprint.h` (new)

**Problem:** Two blueprint implementations coexist. Unclear which is canonical, risks inconsistency.

**Current recommendation (Phase 7+):**
- Treat `src/blueprint_v2/` as canonical for all new work.
- Freeze legacy surface in `src/editor/data/blueprint.h` to bugfix-only.
- Add a bridge/adaptor boundary so runtime/editor entry points consume one canonical model.
- Add migration completion checklist with measurable exit criteria (loader parity, save/load parity, editor parity, test parity).
- Only remove legacy implementation after parity matrix is green.

---

## Medium Priority



### 3. PORTS Macro Bloat
**File:** `src/jit_solver/component.h:61-262`

```cpp
#define PORTS_1(Class, p1) uint32_t p1##_idx = 0;
#define PORTS_2(Class, p1, p2) uint32_t p1##_idx = 0; uint32_t p2##_idx = 0;
// ... continues to PORTS_32
```

**Problem:** 200+ lines of repetitive macro definitions for 1-32 ports.

**Fix Options:**
- Use variadic macro with `__VA_ARGS__` 
- Generate with preprocessor script
- Replace with constexpr template metaprogramming

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

### 10. Sentinel Signal Ordering (NEW)
**File:** `src/jit_solver/jit_solver.cpp:957-994`, `src/jit_solver/simulator.cpp:80-86`

**Problem:** The signal remap places fixed signals at `[N_dyn..N_total-1)` and sentinel
at `[N_total-1]`. But the simulator allocates signals sequentially and marks sentinel as
dynamic (not in `fixed_signals`), causing `dynamic_signals_count` to advance past fixed
signals. The legacy solver then iterates over fixed signals with parasitic conductance — harmless
for RefNodes at constant voltage, but architecturally imprecise.

**Fix Options:**
- Add sentinel index to `fixed_signals` in the build result
- Or have the simulator allocate sentinel as `is_fixed = true`
- Or remap sentinel to be the LAST dynamic signal (before fixed range)

**Impact:** Low (functionally correct in push model)

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

## Summary Table

| # | Issue | Priority | Effort | Status |
|---|-------|----------|--------|--------|
| 1 | Silent OOB in release | ~~High~~ | Low | **FIXED** |
| 2 | Dual blueprint systems | High | High | Open |
| 3 | PORTS macro bloat | Medium | Medium | Open |
| 4 | Magic scheduling numbers | ~~Medium~~ | Low | **FIXED** |
| 5 | ComponentVariant compile time | Medium | High | Open |
| 6 | Alignment verification | Low | Medium | Open |
| 7 | Thread safety audit | Low | High | Open |
| 8 | SimulationState invariants | ~~Low~~ | Low | **FIXED** (partial) |
| 9 | Validation coverage | Low | Medium | Open |
| 10 | Sentinel signal ordering | Medium | Low | **NEW** |
| 13 | Logical phase ordering (Subtract/Splitter) | ~~High~~ | Medium | **FIXED** |
