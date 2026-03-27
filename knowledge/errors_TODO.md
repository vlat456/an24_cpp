# Architecture Issues & TODO

> Issues discovered during codebase analysis. Prioritize and address as needed.

## Resolved

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

All scheduling constants centralized in `SOR_constants.h` under `DomainSchedule` namespace.
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

## High Priority

### 2. Dual Blueprint Systems (Incomplete Migration)
**Files:** 
- `src/editor/data/blueprint.h` (legacy)
- `src/blueprint_v2/blueprint/blueprint.h` (new)

**Problem:** Two blueprint implementations coexist. Unclear which is canonical, risks inconsistency.

**Fix:** 
- Complete migration to blueprint_v2
- Add deprecation warnings to legacy
- Document migration status in AGENTS.md

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
signals. The SOR then iterates over fixed signals with parasitic conductance — harmless
for RefNodes at constant voltage, but architecturally imprecise.

**Fix Options:**
- Add sentinel index to `fixed_signals` in the build result
- Or have the simulator allocate sentinel as `is_fixed = true`
- Or remap sentinel to be the LAST dynamic signal (before fixed range)

**Impact:** Low (functionally correct, just wasteful SOR work on fixed nodes)

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
