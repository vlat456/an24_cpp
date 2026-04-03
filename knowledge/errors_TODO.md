JIT/AOT Electrical Solver — Architectural Review
ISSUE LIST
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
- Location: src/jit_solver/components/port_registry.h:3380-3459
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
3. **All ~78 component headers:** execute/commit/commit_control signatures changed to `double dt`
4. **All ~24 component .cpp files:** method definitions changed to `double dt`
5. **Accumulator variables promoted to double:** `accumulator.h` (state, next_state), `integrator.h` (accumulator, next_accumulator), `pid.h` (integral), `pi.h` (integral), `fuel_tank.h` (level, next_level), `azs.h` (temp), `ru19a.h` (timer, next_timer, current_rpm, next_current_rpm, t4, next_t4), `gs24.h` (wait_time, next_wait_time, current_rpm, next_current_rpm), `inertia_node.h` (rpm, next_rpm), `gidro_accumulator.h` (gas_volume), `battery.h` (capacity, charge)
6. **Type mismatch fixes:** Fixed `std::max`/`std::min`/`std::clamp` calls where promoted `double` accumulators or `double dt` were mixed with `float` literals (azs.cpp, fuel_tank.cpp, gidro_accumulator.cpp, ru19a.cpp, spring.cpp, pd.cpp, pid.cpp, pi.cpp, fast_tmo.cpp, asym_tmo.cpp, monostable.cpp, rug82.cpp)
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
| `gidro_accumulator_regression_tests` | Removed legacy solver matrix/stamp assumptions        |
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

### 17. Replace String-Matched Output Port Classification With Metadata

**File:** `src/jit_solver/jit_solver.cpp:output_ports_for_class()`

**Problem:**

- Topological writer/read classification still relies on hardcoded classname and port-name string lists.
- New components (or new output ports on existing components) can silently be misclassified as inputs if not manually added.
- This already caused a real regression (`RU19A` observation outputs `rpm_out`/`t4_out` missing from writer classification).

**Target architecture:**

- Derive output-port direction from authoritative component metadata rather than handwritten switch-like lists.
- Extend port metadata model so each port includes direction (`input`, `output`, `inout`) in a runtime-queryable form.
- Add explicit component-level `scheduler_source` metadata (boolean) instead of inferring scheduler bucket membership from port shapes.
- Add explicit port-level `source_writer` metadata (boolean) for active source-conflict detection.
- **Strict policy:** no inference, no heuristics, no duck-typing, no legacy fallback paths. Runtime behavior must depend only on strict metadata contracts.

**Detailed TODO plan:**

1. **Port metadata model**
   - Extend `port_registry` API to expose per-port direction (not just names).
   - Add `source_writer` on ports and `scheduler_source` on components in schema + parsed types.
   - Define one source of truth for direction/source-writer/scheduler-source for all components (JIT + AOT compatible).
   - Reject missing required metadata at load/build time (hard fail).
2. **Runtime integration**
   - Replace `output_ports_for_class()` string set heuristics with metadata-driven lookup.
   - Replace active source-conflict detection (`active_source_writer_ports_for`) with explicit `source_writer` metadata lookup.
   - Replace source/consumer scheduler split (`is_scheduler_source_component_class`) with explicit `scheduler_source` metadata lookup.
3. **Validation and fail-fast**
   - Add startup/build-time checks: if a component has unknown/missing required metadata (`direction`, `source_writer`, `scheduler_source`), fail build with explicit error.
   - Remove fallback "shotgun" output-name sets.
   - Remove all inference branches and compatibility/legacy fallback logic.
4. **Tests (required)**
   - Add classification tests for at least: `RU19A`, `GS24`, `ControlledVoltageSource`, `ControlledCurrentSource`, `RefNode`, `Battery`, `Generator`.
   - Add one generic regression that verifies all output ports declared by metadata produce writer edges in topo ordering.
   - Add one generic regression that verifies scheduler source bucket membership is determined only by `scheduler_source` metadata.

**Acceptance criteria:**

- No hardcoded fallback output-name sets remain in `output_ports_for_class()`.
- No string/classname-based source classification remains (`is_scheduler_source_component_class`, `active_source_writer_ports_for` heuristics removed).
- Adding a new component output port or scheduler source requires metadata update only (no jit_solver.cpp edits).
- Full test suite passes, with new direction-driven regression tests.
- Unknown/missing required metadata fails fast (no implicit defaults, no legacy compatibility).

**Impact:** Medium effort, high long-term value (eliminates a bug class).

---

### 18. Remove ParamReader Forwarding Lambdas in `build_systems_dev()`

**File:** `src/jit_solver/jit_solver.cpp` (component build loop)

**Problem:**

- After introducing `ParamReader`, several local lambdas only forward calls (`consume_float_optional`, `consume_bool_optional`, etc.).
- This adds indirection and boilerplate without behavior/value.

**Detailed TODO plan:**

1. Replace forwarding lambdas with direct calls to `param_reader` at component assignment sites.
2. Keep strict-consumption behavior unchanged (`validate_all_consumed()` still called once per component path).
3. For LUT/table and other special-case params, use explicit `param_reader.consume_*` calls directly.
4. Run focused tests:
   - `PushBuildValidation.*`
   - `push_runtime_regression_tests`
   - full suite

**Acceptance criteria:**

- Forwarding lambdas removed.
- Param parsing behavior and error messages unchanged.
- All tests remain green.

**Impact:** Low effort, small readability and maintenance win.

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

**Files:** `src/json_parser/json_parser.cpp:parse_type_definition()`, `src/json_parser/json_parser.cpp:load_type_registry()`

**Problem:** Two separate parsers with different strictness — scheduler_source was missing from lenient path.

**Resolution:** The key regression (`scheduler_source` missing from `parse_type_definition`) is fixed and has regression tests:

- `test_push_build_validation.cpp` lines 740-763: scheduler_source regression
- `json_parser_test.cpp` lines 883-919: scheduler_source parity tests

Remaining differences are intentional — `parse_type_definition` is a lenient test-only path for legacy format (`classname`, `ports`, `params`), while `load_type_registry` is the strict production path for v3 format (`id`, `interface`, `param_defaults`). Full unification would introduce medium risk with minimal gain.

**Impact:** Resolved — no blocking drift risk with regression tests in place.

---

### 21. Strengthen Unknown-Class Fail-Fast in Metadata API

**Files:** generated `src/jit_solver/components/port_registry.h`, `src/jit_solver/jit_solver.cpp`

**Problem:**

- Generated helper `is_scheduler_source_component(classname)` returns `false` for unknown class.
- JIT path currently guards with `has_component_metadata()` before use, but helper behavior can hide misuse in other call sites.

**Detailed TODO plan:**

1. Add debug assert or explicit checked helper variant in generated API that requires known classname.
2. Replace any unchecked call sites with checked variant.
3. Add tests ensuring unknown classnames trigger explicit failure in strict paths.

**Acceptance criteria:**

- Unknown classnames cannot silently downgrade to "consumer" classification.
- All runtime classification paths fail fast with explicit errors for unknown metadata.

**Impact:** Low effort, prevents silent misclassification.

---

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

### ~~10. Sentinel Signal Ordering~~ ✓ FIXED

**File:** `src/jit_solver/jit_solver.cpp`

**Problem:** Sentinel was allocated as dynamic, causing `dynamic_signals_count` to advance
past fixed signal range.

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

## Summary Table

| #     | Issue                                      | Priority   | Effort | Status              |
| ----- | ------------------------------------------ | ---------- | ------ | ------------------- |
| 1     | Silent OOB in release                      | ~~High~~   | Low    | **FIXED**           |
| 2     | Dual blueprint systems                     | High       | High   | Open                |
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
| DT    | float dt → double dt conversion           | ~~Medium~~ | Medium | **COMPLETED**      |
| 22    | Extract functional blueprints to C++      | Medium     | Medium | **OPEN**           |
