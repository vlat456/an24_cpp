# AOT-First Electrical Refinement Plan (Consensus)

Date: 2026-03-31
Status: revised after expert consultation + technical review

## Intent

Electrical subsolver MVP is complete and stable. The next goal is to improve:

- elegance (clear boundaries, less transitional complexity)
- AOT performance (no avoidable indirection in hot path)

Primary constraint: preserve validated electrical semantics and runtime stability.

---

## Executive Summary

The first draft underestimated three things:

1. semantic freeze needs more precision than "same stamping/signs"
2. test migration off direct `build_systems_dev()` fallback is a substantial workstream
3. kernel specialization should happen later, after parity and observability are stable

A fourth underestimate was identified on review:

4. codegen currently emits zero electrical solver logic — generated `step_N()` methods
   only call `execute()` per component. There is no generated call to `solve_electrical()`,
   no generated `ElectricalBuildPlan`, and no generated `ElectricalRuntimeState`.
   Phase 1 is therefore a net-new codegen feature, not a small plumbing change.

Consensus direction:

- freeze semantics first
- generate static AOT electrical plan first
- run shared generic solver first
- add stable symbolic bindings (directly tied to AOT indirection removal)
- add observability tooling
- only then apply targeted specialization under strict budget controls

---

## Non-Negotiable Principles

1. One electrical model, two execution backends:
   - JIT/editor: flexible and safe for malformed in-flight topologies
   - AOT/runtime: static, generated, low-indirection

2. AOT hot path targets:
   - no string/hash lookups
   - no variant visitation in solve loops
   - no per-frame heap allocation in electrical solve

3. Semantic reference must be explicit and testable.

4. Deletion is gated by coverage, not preference.

---

## Implementation Log

### Phase 0: Semantic Freeze — ✅ COMPLETE (2026-03-31)

- **Step 1**: Created `knowledge/22_electrical_semantics.md` — documentation-only semantic freeze
  covering: branch current direction conventions, source polarity rules, fixed-node behavior,
  singular/ill-conditioned island handling, pivot/threshold policy, float precision rules,
  island extraction, phase ordering, and codegen implications.
  - Review found Norton polarity was backwards in draft (injecting `+I_n` into `node_a`
    instead of `node_b`). Fixed before commit.
- **Step 2**: ✅ DONE — moved per-island scratch vectors (`fixed_nodes`, `island_nodes`,
  `fixed_voltages`, `is_fixed`, `node_to_unknown`, `island_voltages`) into
  `ElectricalRuntimeState`. Zero per-island heap allocations in solve path.
- **Step 3**: ✅ DONE — added `tests/test_electrical_parity_fixtures.cpp` with 5 JIT reference
  fixtures. All 5 pass (SimpleTheveninDivider, SeriesChainTwoResistors,
  ParallelBranchSplit, MultiIsland, NearShortHighConductance). Also verified
  existing 10 `electrical_subsolver_tests`, 14 `electrical_primitives_tests`,
  8 `battery_discharge_tests`, and 6 `current_sense_tests` all pass after
  scratch buffer refactor. Build verified after `-ffast-math` workaround.
- **Step 4**: (pending — electrical plan emission in `codegen.cpp`)

### Phase 1: Generated Static Electrical Plan — ✅ COMPLETE (2026-03-31)

Implementation log:

- **Step 4 (Phase 1 bootstrap)**: ✅ DONE — electrical plan emission in codegen
  - Added `ElectricalElementKindCodegen`, `ElectricalElementCodegen`, `ElectricalIslandPlanCodegen`,
    `ElectricalPlanCodegen` structs to `codegen.h`
  - Added `extract_electrical_plan()` to `codegen.cpp` — mirrors island extraction from
    `build_systems_dev()` using union-find over connected ports; supports both `solver_role`
    metadata path and classname fallback (Battery, Generator, RefNode, Resistor,
    IndicatorLight, CurrentSense, ElectricalConductance, ElectricalSource)
  - Updated `generate_header()`: emits `constexpr ElectricalElement` arrays per island
    (`island_N_elements[]`), signal index arrays (`island_N_nodes[]`),
    `ELECTRICAL_ISLAND_COUNT`, and `AotElectricalPlan` struct (builds
    `ElectricalBuildPlan` from static arrays at init). Adds `AotElectricalPlan electrical_plan_`
    and `ElectricalRuntimeState electrical_rt_` members to generated class.
  - Updated `generate_source()`: added `#include "jit_solver/subsolvers/electrical_subsolver.h"`;
    modified each generated `step_N()` to call `solve_electrical()` BEFORE component
    `execute()` calls.
  - Updated `generate_composite_systems()`: calls `extract_electrical_plan()` and passes
    it to `generate_header()`/`generate_source()`
  - Updated `write_files()` public API: accepts optional `ElectricalPlanCodegen`;
    auto-extracts if not provided, enabling the old flat-signal codegen path to also
    benefit from electrical plan emission
  - Used `spdlog::warn` (via `spdlog::spdlog` linked to `an24_codegen`) for
    unresolvable electrical ports in `extract_electrical_plan()` — follows existing
    spdlog conventions in the codebase
  - Added 3 codegen tests in `test_aot_composite.cpp`:
    - `ElectricalPlan_BatteryAndResistor_GeneratesIslandArrays`
    - `ElectricalPlan_IndicatorLight_GeneratesConductanceBranch`
    - `ElectricalPlan_NoElectricalDevices_HasZeroIslands`
  - **AOT parity tests**: Added 5 `ElectricalAotParity` tests comparing JIT vs codegen path
    results using the same fixtures as `ElectricalParityFixtures`. `run_aot_electrical()` helper
    calls `extract_electrical_plan()` → converts to `ElectricalBuildPlan` → runs `solve_electrical()`
    → compares all signal values. All 5 pass, proving `extract_electrical_plan()` produces
    identical electrical model to `build_systems_dev()`.
  - **Scratch buffer pre-allocation**: Added `reserve()` method to `ElectricalRuntimeState`
    for pre-allocating all scratch buffers to max island sizes. Codegen emits
    `ELECTRICAL_MAX_ISLAND_NODES/ELEMENTS/MAX_COMPONENT_INDEX` constants and calls
    `electrical_rt_.reserve()` in constructor. Tightens allocation bound from `signal_count`
    to actual max island size (typically 3-15 vs 200+).
  - All 5 `ElectricalParityFixtures` tests pass
  - All 6 `AotComposite` tests pass
  - All 5 `ElectricalAotParity` tests pass
  - 7 pre-existing test failures unrelated to electrical/codegen changes

---

## Current Baseline

Completed in MVP:

- electrical island extraction and solve
- solver-owned electrical propagation
- branch-current bindings for Battery/CurrentSense
- primitive nodes (`ElectricalSource`, `ElectricalConductance`)
- metadata path (`solver_role`) plus fallback for direct builder tests
- broad regression coverage including real fixture behavior

Known gaps in current implementation:

- codegen (`codegen.cpp`) has zero awareness of electrical islands — generated step methods
  call only `execute()` per device. No generated `solve_electrical()` call exists. ✅ FIXED (Phase 1 Step 4)
- `solve_electrical()` allocates multiple `std::vector` temporaries per island per frame
  (`island_nodes`, `fixed_nodes`, `is_fixed`, `node_to_unknown`, `island_voltages`).
  This violates the "no per-frame heap allocation" target. ✅ FIXED (Phase 0 Step 2)
- `ElectricalRuntimeState` scratch buffers reuse capacity but the per-island local vectors do not.
  ✅ FIXED (Phase 1): `reserve()` method added; codegen pre-allocates to max island size.
- Stable symbolic bindings (Phase 2) not yet implemented
- Observability/diagnostics for electrical failures (Phase 3) not yet implemented
- `write_files()` public API doesn't call `merge_device_instance` — only works when
  devices already have ports defined inline (test fixtures using `generate_composite_systems`
  must register types in registry with ports defined)

---

## Phase Plan (Revised)

## Phase 0: Semantic Freeze + Parity Fixtures

Must-have:

- short electrical semantics spec covering:
  - branch current direction conventions
  - source polarity rules
  - fixed-node behavior
  - singular/ill-conditioned island behavior
  - pivot/threshold policy
  - float/double rules for accumulators
- canonical parity fixture set (JIT reference vs AOT path):
  - simple source+load+ref
  - parallel branch split
  - mixed wrapper+primitive island
  - singular malformed island
  - near-short/high-conductance case
- baseline benchmark command and reproducible metric capture

Exit gate:

- semantics spec is a committed document (can be a `.md` in `knowledge/`)
- every convention in the spec has at least one parity fixture exercising it
- baseline benchmark produces a numeric result that can be compared across runs

## Phase 1: Generated Static Electrical Plan, Shared Solver

This is the largest phase. Current codegen has zero electrical awareness.

Must-have:

- codegen emits static electrical island plan arrays:
  - nodes
  - fixed constraints
  - branches (kind, node_a, node_b, value_a, value_b, component_index)
  - component-binding references
- generated step methods call `solve_electrical()` with the generated plan
  at the correct point in the phase sequence (before component execute,
  matching JIT simulator step flow)
- generated code owns `ElectricalRuntimeState` (scratch buffers, branch currents)
- eliminate per-island heap allocation in `solve_electrical()`:
  - pre-allocate `island_nodes`, `is_fixed`, `node_to_unknown`, `island_voltages`
    as scratch arrays in `ElectricalRuntimeState` (sized to max island node count)
  - this is required before any performance claims are meaningful
- AOT runtime consumes generated plan using shared generic solve logic
- stable symbolic IDs (separate from solve-order indices)

Nice-to-have:

- schema/version hash for generated plan in debug builds

Key risk:

- The shared `solve_electrical()` function signature currently takes `ElectricalBuildPlan`
  which contains `std::vector` members. For AOT, the generated plan should use
  `constexpr` / `static const` arrays. Either the solver accepts a view/span-based
  interface, or a thin adapter converts static arrays to the existing types at init.
  Prefer the span-based approach (lower indirection) but do not block Phase 1 on it
  if the adapter approach ships faster.

Exit gate: ✅ ALL MET (2026-03-31)

- ✅ AOT path no longer depends on metadata/string lookups at runtime — codegen emits
  static electrical plan arrays consumed by shared `solve_electrical()`
- ✅ generated code compiles and passes all Phase 0 parity fixtures — 5 `ElectricalParityFixtures`
  (JIT) + 5 `ElectricalAotParity` (JIT vs codegen comparison) all pass
- ✅ no per-frame heap allocation in `solve_electrical()` — scratch buffers pre-allocated via
  `reserve()` to max island sizes

## Phase 2: Direct Symbolic Observer/State Bindings

Rationale: this phase directly removes dynamic name→index lookups from the AOT
step, which is the project's primary performance goal. Observability (Phase 3)
is important but does not reduce indirection, so it comes after.

Must-have:

- generated stable bindings for Battery, CurrentSense, and observers
- runtime reads by generated symbolic handle/offset, not dynamic name lookup
- tests proving binding correctness survives harmless plan reorderings

Exit gate:

- no runtime device-name -> branch-index mapping in AOT step

Implementation log:

- **Step 1 (Phase 2 bootstrap)**: ✅ DONE — generated stable symbolic electrical bindings
  - Added `ElectricalExtractOptions` and extraction options plumbing in `codegen.h`
    and `electrical_codegen.cpp`
  - Added deterministic symbolic binding extraction in `extract_electrical_plan()`:
    wrapper devices (`Battery`, `Generator`, `IndicatorLight`, `CurrentSense`) now
    emit stable `{device_field_name, island_index, element_index, component_index}`
    records in `ElectricalPlanCodegen::device_bindings`
  - Updated generated class emission in `codegen.cpp`:
    - emits `ElectricalBindings` static constants per wrapper device
    - writes wrapper `electrical_handle.{island_index, element_index, component_index}`
      in constructor from generated constants (no runtime name lookup)
  - Kept AOT step pointer hygiene: generated steps now clear
    `st->electrical_rt = nullptr` after execute pass
  - Regression suite passed:
    - 9/9 `AotComposite`
    - 5/5 `ElectricalParityFixtures`
    - 5/5 `ElectricalAotParity`
    - 1/1 `JitAotBridgeEquivalence`

- **Step 2 (Phase 2 validation)**: ✅ DONE — binding stability regression tests
  - Added `AotComposite.ElectricalBindings_WrapperHandlesGenerated`
    in `tests/test_aot_composite.cpp`:
    - verifies generated header contains `ElectricalBindings` constants
      for wrapper devices
    - verifies generated source assigns `electrical_handle.component_index`
      from generated symbolic constants
  - Added `AotComposite.ElectricalBindings_StableAcrossConnectionReordering`
    in `tests/test_aot_composite.cpp`:
    - builds two topologically identical circuits with reordered connection lists
    - verifies generated binding constants (`bat_component`, `sense_component`)
      remain identical across reorderings
    - verifies generated handle assignment lines remain present in both outputs
  - Validation passed:
    - 6/6 targeted `AotComposite` tests (including new binding tests)
    - 5/5 `ElectricalParityFixtures`
    - 5/5 `ElectricalAotParity`
    - 1/1 `JitAotBridgeEquivalence`

- **Step 3 (Phase 2 hardening)**: ✅ DONE — full handle-field symbolic assignment checks
  - Added `AotComposite.ElectricalBindings_AssignAllHandleFieldsFromConstants`
    in `tests/test_aot_composite.cpp`
  - Verifies generated header contains symbolic constants for all three handle
    fields (`*_island`, `*_element`, `*_component`) for wrapper devices
  - Verifies generated source assigns all three runtime handle fields from those
    constants for `Battery` and `CurrentSense`
  - Validation passed:
    - 3/3 `AotComposite.ElectricalBindings*`
    - 5/5 `ElectricalParityFixtures`
    - 5/5 `ElectricalAotParity`
    - 1/1 `JitAotBridgeEquivalence`

- **Step 4 (Phase 2 strict review + bugfix)**: ✅ DONE — 2-pass review complete
  - Ran strict 2-pass review (`@review`) before entering Phase 3
  - Found and fixed latent high-severity mapping bug in
    `src/codegen/electrical_codegen.cpp`:
    - root cause: binding generation incorrectly indexed `devices[component_index]`
      where `component_index` is electrical-element ordinal, not device-array index
      when non-electrical devices are interleaved
    - fix: store `device_name` + `device_classname` in raw electrical elements and
      derive symbolic bindings from those stored fields via component→raw lookup
  - Added regression test:
    - `AotComposite.ElectricalBindings_MixedDevicesCorrectMapping`
    - verifies correct wrapper binding names with interleaved non-electrical device
      and guards against accidental non-electrical handle assignment
  - Full required pre-Phase-3 regression suite passed:
    - `AotComposite` (all, including new test)
    - `ElectricalParityFixtures`
    - `ElectricalAotParity`
    - `JitAotBridgeEquivalence`
    - `LUTCodegen`
    - `CodegenAccumulator` (enabled subset)
  - Phase 2 ready for Phase 3

## Phase 3: Observability

Must-have:

- generated debug mapping:
  - branch ID -> device/role/endpoints
  - node ID -> signal/port identity
- structured island diagnostics for failures/parity mismatches:
  - island id
  - offending node/branch ids
  - solved/residual values

Nice-to-have:

- generated source comments/source-map style traceability in debug

Exit gate:

- any parity failure is diagnosable from logs without ad-hoc instrumentation

Implementation log:

- **Step 1 (Phase 3 bootstrap)**: ✅ DONE — generated electrical debug mapping
  - Extended `ElectricalPlanCodegen` with per-component debug entries:
    `{component_index, device_name, device_classname, role, node_a, node_b}`
  - `extract_electrical_plan()` now fills `component_debug` deterministically
    from extracted electrical elements
  - `generate_header()` now emits observability tables in generated code:
    - `struct ElectricalDebugEntry`
    - `constexpr ElectricalDebugEntry ELECTRICAL_DEBUG_MAP[]`
    - `constexpr uint32_t ELECTRICAL_DEBUG_COUNT`
  - Added regression test:
    - `AotComposite.ElectricalDebugMap_ContainsRoleAndEndpoints`
    - verifies generated debug map includes wrapper names, classnames, and role tags
  - Validation passed:
    - `AotComposite.ElectricalBindings*` + `ElectricalDebugMap*`
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)
    - `JitAotBridgeEquivalence` (1/1)

- **Step 2 (Phase 3 diagnostics)**: ✅ DONE — structured island diagnostics
  - Extended runtime diagnostics in `ElectricalRuntimeState`:
    - `IslandDiagnostic` records: island id, solve_ok, unknown_count,
      worst-node signal/voltage, max abs KCL residual, worst branch component index
    - scratch residual vector `kcl_residuals`
  - `solve_electrical()` now computes and stores per-island diagnostics every solve:
    - branch-current extrema tracking
    - per-node KCL residual accumulation and max residual extraction
  - Generated AOT solve path now logs structured diagnostics when:
    - island solve fails, or
    - residual exceeds threshold (`ELECTRICAL_DIAG_RESIDUAL_WARN = 1e-4f`)
  - Diagnostic logs include branch metadata via generated `ELECTRICAL_DEBUG_MAP`
    (component index -> device/class/role/endpoints)
  - Added regression test:
    - `AotComposite.ElectricalDiagnostics_WarnPathGenerated`
    - verifies warn-path code generation and debug-map correlation hooks
  - Validation passed:
    - `AotComposite.ElectricalBindings*`, `ElectricalDebugMap*`, `ElectricalDiagnostics*`
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)
    - `JitAotBridgeEquivalence` (1/1)

- **Step 3 (Phase 3 traceability hardening)**: ✅ DONE — island/element trace path
  - Extended generated debug entries with structural location keys:
    - `island_index`
    - `element_index`
  - Added generated helper in AOT class:
    - `dump_island_debug(uint32_t island_idx)`
    - logs all debug entries for that island using generated
      `ELECTRICAL_DEBUG_MAP`
  - Warn path now calls `dump_island_debug(diag.island_index)` for richer,
    directly attributable failure context
  - Added regression test:
    - `AotComposite.ElectricalDebugMap_ContainsIslandAndElementIndices`
    - verifies generated output includes island/element debug keys and
      island-filtered dump path
  - Validation passed:
    - `AotComposite.ElectricalBindings*`
    - `AotComposite.ElectricalDebugMap*`
    - `AotComposite.ElectricalDiagnostics*`
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)
    - `JitAotBridgeEquivalence` (1/1)

- **Step 4 (Phase 3 strict 2-pass review + bugfix)**: ✅ DONE
  - Ran strict 2-pass review (`@review`) over Phase 3 implementation and
    full required regression suites
  - Found and fixed high-severity latent bug in AOT parity test helper:
    `run_aot_electrical()` allocated only max-signal+1 slots, while JIT path
    includes an additional sentinel slot (`next_signal + 1`), causing potential
    out-of-bounds reads in parity comparisons
  - Fix applied in `tests/test_electrical_parity_fixtures.cpp`:
    - allocate sentinel slot to match JIT builder semantics
    - remove unused `connections` helper parameter
  - Post-fix required suites all pass:
    - `AotComposite` (all)
    - `ElectricalParityFixtures`
    - `ElectricalAotParity`
    - `JitAotBridgeEquivalence`
    - `LUTCodegen`
    - `CodegenAccumulator` (enabled subset)
  - Phase 3 marked ready

## Phase 4: Test Infrastructure Migration + Fallback Boundary

Note: this workstream is largely independent and can start in parallel with
Phases 1-3. It is numbered Phase 4 only because it must complete before Phase 5
(specialization), not because it must wait for Phase 3 to finish.

Must-have:

- explicitly split tests into:
  - raw-builder tests (intentional direct `build_systems_dev()`)
  - production-path tests (library merge/metadata/codegen path)
- migrate critical electrical parity tests to production-path helpers
- track remaining fallback-dependent tests as explicit debt

Exit gate:

- fallback extraction use is measured, scoped, and no longer accidental

Implementation log:

- **Step 1 (Phase 4 bootstrap)**: ✅ DONE — explicit raw-builder vs production-path split
  - Added test labels in `tests/CMakeLists.txt`:
    - `raw_builder;electrical` for raw extraction/build tests
    - `production_path;electrical` for merged/codegen-driven tests
  - Applied labels to key electrical suites:
    - raw-builder: `electrical_island_build_tests`, `electrical_handle_build_tests`,
      `electrical_primitives_tests`, `electrical_parity_fixtures_tests`
    - production-path: `aot_composite_tests`, `jit_aot_bridge_equivalence_tests`
  - Added new production-path parity suite:
    - `tests/test_production_path_parity.cpp`
    - test: `ProductionPathParity.CompositeAotJitTopologyParity`
    - validates expanded/merged composite topology parity against AOT generation
      while exercising generated electrical debug-map presence
  - Measured current split baseline via ctest labels:
    - `production_path`: 17 tests
    - `raw_builder`: 10 tests
  - Validation passed:
    - `AotComposite` (all)
    - `JitAotBridgeEquivalence` (1/1)
    - `ProductionPathParity` (1/1)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)

- **Step 2 (Phase 4 migration)**: ✅ DONE — migrate critical multi-island parity to production path
  - Extended `tests/test_production_path_parity.cpp` with:
    - `ProductionPathParity.MultiIslandDebugAndPlanParity`
  - New production-path test validates for multi-island topology:
    - JIT detects 2 electrical islands after merge/expand path
    - AOT generated header encodes `ELECTRICAL_ISLAND_COUNT = 2`
    - generated diagnostics/debug hooks are present (`ELECTRICAL_DEBUG_MAP`,
      `dump_island_debug(diag.island_index)`)
  - Updated measured split baseline:
    - `production_path`: 18 tests (was 17)
    - `raw_builder`: 10 tests
  - Validation passed:
    - `AotComposite` (all)
    - `JitAotBridgeEquivalence` (1/1)
    - `ProductionPathParity` (2/2)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)

- **Step 3 (Phase 4 debt accounting)**: ✅ DONE — explicit raw-builder debt tracker
  - Added `knowledge/phase4_raw_builder_debt.md` with:
    - measured baseline callsites: `build_systems_dev(...)` in `tests/*.cpp`
    - per-file raw-builder usage ranking
    - intentional raw-builder vs migration-candidate categorization
    - operational migration steps and measurable exit criteria
  - Added index entry in `knowledge/index.md`
  - Current measured baseline (2026-03-31, verified):
    - raw-builder callsites: 107 (non-comment invocations)
    - CTest labels: `production_path`=18, `raw_builder`=10

- **Step 4 (Phase 4 migration)**: ✅ DONE — migrate port-map regressions to production path
  - Added `tests/test_production_path_port_map.cpp` with 3 production-path tests:
    - `ProductionPathPortMap.AndGateReadsWiredInputs`
    - `ProductionPathPortMap.NotGateReadsCorrectInput`
    - `ProductionPathPortMap.SubtractReadsBothInputs`
  - New suite uses `JIT_Simulator::start_from_json()` + runtime stepping to validate
    wiring semantics on merged/expanded production path (not direct raw builder)
  - Added `production_path_port_map_tests` target in `tests/CMakeLists.txt`
    labeled as `production_path`
  - Updated debt tracker baseline:
    - `production_path` tests: 22 (was 18)
    - `raw_builder` tests: 10
  - Validation passed:
    - `AotComposite` (all)
    - `JitAotBridgeEquivalence` (1/1)
    - `ProductionPathParity` (2/2)
    - `ProductionPathPortMap` (3/3)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)

- **Step 5 (Phase 4 migration)**: ✅ DONE — migrate push-runtime regressions to production path
  - Added `tests/test_production_path_push_runtime.cpp` with 2 production-path tests:
    - `ProductionPathPushRuntime.SinglePassSettlesLinearChain`
    - `ProductionPathPushRuntime.CycleRemainsFinite`
  - New suite uses `JIT_Simulator::start_from_json()` + stepping to validate
    runtime behavior without direct `build_systems_dev(...)` wiring in test body
  - Added `production_path_push_runtime_tests` target in `tests/CMakeLists.txt`
    labeled as `production_path`
  - Updated debt-tracker baseline:
    - `production_path` tests: 24 (was 22)
    - `raw_builder` tests: 10
  - Validation passed:
    - `AotComposite` (all)
    - `JitAotBridgeEquivalence` (1/1)
    - `ProductionPathParity` (2/2)
    - `ProductionPathPortMap` (3/3)
    - `ProductionPathPushRuntime` (2/2)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)

- **Step 6 (Phase 4 strict gate review + hygiene fixes)**: ✅ DONE
  - Ran strict 2-pass `@review` across whole Phase 4 + cross-phase interactions
  - Review gate decision: **READY for Phase 5** (no blocking bugs)
  - Applied two non-functional hygiene fixes from review notes:
    - `tests/test_electrical_subsolver.cpp`: add assertions for
      `island_diagnostics` fields (`solve_ok`, `unknown_count`, residual bound)
      in `ElectricalSubsolver.SimpleTheveninDivider`
    - `tests/test_jit_aot_bridge_equivalence.cpp`: clarify smoke-test scope
      (structural codegen test, not numeric parity)
  - Re-ran required regression suites (+ touched subsolver suite):
    - `AotComposite` (all)
    - `ProductionPathParity` (all)
    - `ProductionPathPortMap` (all)
    - `ProductionPathPushRuntime` (all)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)
    - `JitAotBridgeEquivalence` (1/1)
    - `LUTCodegen` (9/9)
    - `CodegenAccumulator` enabled subset (5/5, 4 disabled)
    - `ElectricalSubsolver` (10/10)

## Phase 5: Targeted Kernel Specialization (Budgeted)

Must-have:

- specialize only hot/common island shapes
- keep generic solver path as correctness fallback
- enforce hard budgets on:
  - number of specialized families
  - compile-time growth
  - generated code size growth
- parity validation against shared solver for every specialized path

Nice-to-have:

- unrolled kernels for only top 2-3 size classes

Exit gate:

- measurable AOT speedup on representative graphs with acceptable build/binary cost

Implementation log:

- **Step 1 (Phase 5 bootstrap)**: ✅ DONE — pre-specialization budget + baseline gate
  - Entered Phase 5 only after strict Phase 4 gate review (`Step 6`) passed
  - Baseline constraints carried into specialization work:
    - Keep generic `solve_electrical()` as correctness fallback
    - Keep specialization count budgeted to top 2–3 island families max
    - Preserve parity guardrails (`ElectricalAotParity`, `AotComposite`,
      production-path suites)
  - No specialization kernels added in this step; this is the gate + scope lock
    before introducing specialized dispatch

- **Step 2 (Phase 5 first specialized family)**: ✅ DONE — N==1 solve fast path
  - Added first bounded specialization in `solve_electrical()`:
    - when dense unknown count `N == 1`, solve directly with scalar divide
      (`b[0] /= A[0]`) and singular guard (`|A[0]| < 1e-12` => fallback)
    - keep generic Gaussian path unchanged for `N > 1`
  - This specialization preserves existing fallback semantics:
    - on singular matrix, `solve_ok=false` and previous-state voltage retention
  - Added regression test in `tests/test_electrical_subsolver.cpp`:
    - `ElectricalSubsolver.SpecializedN1SolveMatchesExpectedDivider`
    - validates expected divider voltage and diagnostics fields for N==1 case
  - Validation passed:
    - `ElectricalSubsolver` (11/11)
    - `AotComposite` (all)
    - `ProductionPathParity` (2/2)
    - `ProductionPathPortMap` (3/3)
    - `ProductionPathPushRuntime` (2/2)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)
    - `JitAotBridgeEquivalence` (1/1)
    - `LUTCodegen` (9/9)
    - `CodegenAccumulator` enabled subset (5/5, 4 disabled)

- **Step 3 (Phase 5 second specialized family)**: ✅ DONE — N==2 solve fast path
  - Added second bounded specialization in `solve_electrical()`:
    - when dense unknown count `N == 2`, solve via closed-form 2x2 inverse
      with determinant singular guard (`|det| < 1e-12` => fallback)
    - preserve generic Gaussian path for `N > 2`
  - Preserved fallback semantics and diagnostics behavior on singular cases
  - Added regression in `tests/test_electrical_subsolver.cpp`:
    - `ElectricalSubsolver.SpecializedN2SolveMatchesSeriesChain`
    - validates expected 2-unknown solve voltages and diagnostics residual bound
  - Validation passed:
    - `ElectricalSubsolver` specialized tests (`N1`, `N2`)
    - `AotComposite` (all)
    - `ProductionPathParity` (2/2)
    - `ProductionPathPortMap` (3/3)
    - `ProductionPathPushRuntime` (2/2)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)
    - `JitAotBridgeEquivalence` (1/1)
    - `LUTCodegen` (9/9)
    - `CodegenAccumulator` enabled subset (5/5, 4 disabled)

- **Step 4 (Phase 5 instrumentation)**: ✅ DONE — specialization hit counters
  - Extended `ElectricalRuntimeState` with `SolveCounters`:
    - `islands_total`, `solves_n0`, `solves_n1`, `solves_n2`, `solves_dense`,
      `singular_fallbacks`
  - `solve_electrical()` now resets/fills counters each solve call and tracks
    which kernel family executed per island
  - Added regression in `tests/test_electrical_subsolver.cpp`:
    - `ElectricalSubsolver.SolveCountersTrackSpecializedPaths`
    - validates counter distribution across mixed N=0/N=1/N=2/singular islands
  - Validation passed:
    - `ElectricalSubsolver` specialized + counter tests
    - `AotComposite` (all)
    - `ProductionPathParity` (2/2)
    - `ProductionPathPortMap` (3/3)
    - `ProductionPathPushRuntime` (2/2)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)
    - `JitAotBridgeEquivalence` (1/1)
    - `LUTCodegen` (9/9)
    - `CodegenAccumulator` enabled subset (5/5, 4 disabled)

- **Step 5 (Phase 5 observability tie-in)**: ✅ DONE — periodic AOT counter logs
  - Extended generated AOT solve path to emit periodic specialization summary:
    - added `ELECTRICAL_COUNTER_LOG_PERIOD = 600` frames
    - every period logs `ElectricalRuntimeState::counters` via spdlog:
      islands total, N0/N1/N2/dense hits, singular fallback count
  - Added/extended regressions:
    - `AotComposite.ElectricalDiagnostics_WarnPathGenerated` now verifies
      generated counter-log hook strings
    - `ElectricalSubsolver.SolveCountersTrackSpecializedPaths` validates
      runtime counter values for mixed island families
  - Validation passed:
    - `ElectricalSubsolver` (all, incl. N1/N2/counter tests)
    - `AotComposite` (all)
    - `ProductionPathParity` (2/2)
    - `ProductionPathPortMap` (3/3)
    - `ProductionPathPushRuntime` (2/2)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)
    - `JitAotBridgeEquivalence` (1/1)
    - `LUTCodegen` (9/9)
    - `CodegenAccumulator` enabled subset (5/5, 4 disabled)

- **Step 6 (Phase 5 budget decision)**: ✅ DONE — hold at N1/N2, keep dense fallback
  - Added explicit dense-path coverage regression:
    - `ElectricalSubsolver.SolveCountersTrackDensePathForN3`
    - validates N==3 islands route to dense generic kernel (`solves_dense=1`)
  - Decision based on counters + risk budget:
    - keep specialization families at **N==1** and **N==2** for now
    - do **not** add third specialized family in this phase
    - preserve dense generic path as correctness fallback for N>=3
  - Rationale:
    - bounded complexity and code growth
    - full parity matrix remains green
    - instrumentation now in place to justify future expansion empirically
  - Validation passed:
    - `ElectricalSubsolver` specialized + dense counter tests
    - `AotComposite` (all)
    - `ProductionPathParity` (2/2)
    - `ProductionPathPortMap` (3/3)
    - `ProductionPathPushRuntime` (2/2)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)
    - `JitAotBridgeEquivalence` (1/1)
    - `LUTCodegen` (9/9)
    - `CodegenAccumulator` enabled subset (5/5, 4 disabled)

## Phase 6: Transitional Cleanup + Performance Closure

Must-have:

- remove/de-scope transitional fallback code only after gates pass
- remove dead compatibility paths introduced during migration
- final profiling and optimization sweep on representative aircraft graphs

Exit gate:

- zero per-frame heap allocations in AOT electrical solve path
- zero string/hash lookups in AOT electrical solve path
- AOT electrical step time is within 2× of a no-op baseline on same graph
  (or a tighter target established by Phase 0 benchmark)
- no dead fallback code remains in active compilation paths

Implementation log:

- **Step 1 (Phase 6 kickoff)**: ✅ DONE — cleanup scope lock + closure checklist
  - Carry-over from Phase 5 decision:
    - keep specialization budget at `N==1` / `N==2`
    - keep dense generic fallback for `N>=3`
  - Cleanup targets for Phase 6:
    - identify transitional diagnostics and temporary compatibility paths
      that were added during Phases 1–5 and are now candidates for pruning
    - keep production-path parity suites as release guardrails during cleanup
    - preserve explicit `raw_builder` vs `production_path` test boundary
  - Closure checklist established:
    1. verify no per-frame allocations in AOT electrical solve path regressions
    2. verify no runtime string/hash lookups on AOT electrical hot path
    3. confirm no dead transitional code remains in active compilation paths
    4. run full parity + production-path regression matrix before final closure

- **Step 2 (Phase 6 cleanup)**: ✅ DONE — deduplicate codegen sanitization helper
  - Added shared utility header: `src/codegen/codegen_utils.h`
    - `sanitize_codegen_name(...)`
  - Removed duplicated local sanitization implementations between
    `codegen.cpp` and `electrical_codegen.cpp`
  - Updated both code paths to use shared helper, reducing drift risk during
    future cleanup/maintenance
  - Validation passed:
    - `ElectricalSubsolver` (all)
    - `AotComposite` (all)
    - `ProductionPathParity` (2/2)
    - `ProductionPathPortMap` (3/3)
    - `ProductionPathPushRuntime` (2/2)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)
    - `JitAotBridgeEquivalence` (1/1)
    - `LUTCodegen` (9/9)
    - `CodegenAccumulator` enabled subset (5/5, 4 disabled)

- **Step 3 (Phase 6 cleanup)**: ✅ DONE — deduplicate test execution helper
  - Added shared test helper header: `tests/test_helpers.h`
    - centralized `make_execution(...)` helper for `ExecutionPhases`
  - Removed duplicated local `make_execution(...)` implementations from:
    - `tests/test_aot_composite.cpp`
    - `tests/test_production_path_parity.cpp`
    - `tests/test_jit_aot_bridge_equivalence.cpp`
  - Preserved test behavior while reducing maintenance drift risk in
    production-path/codegen validation suites
  - Validation passed:
    - `ElectricalSubsolver` (all)
    - `AotComposite` (all)
    - `ProductionPathParity` (2/2)
    - `ProductionPathPortMap` (3/3)
    - `ProductionPathPushRuntime` (2/2)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)
    - `JitAotBridgeEquivalence` (1/1)
    - `LUTCodegen` (9/9)
    - `CodegenAccumulator` enabled subset (5/5, 4 disabled)

- **Step 4 (Phase 6 cleanup)**: ✅ DONE — remove dead RefNode fallback branch
  - Removed transitional/dead compatibility branch in `codegen.cpp` fixed-signal
    emission (`RefNode` port lookup fallback from `.v_out` to `.v`)
  - `RefNode` canonical port remains `.v`; generated fixed-signal emission now
    uses only canonical mapping, reducing dead-path ambiguity
  - Validation passed:
    - `ElectricalSubsolver` (all)
    - `AotComposite` (all)
    - `ProductionPathParity` (2/2)
    - `ProductionPathPortMap` (3/3)
    - `ProductionPathPushRuntime` (2/2)
    - `ElectricalParityFixtures` (5/5)
    - `ElectricalAotParity` (5/5)
    - `JitAotBridgeEquivalence` (1/1)
    - `LUTCodegen` (9/9)
    - `CodegenAccumulator` enabled subset (5/5, 4 disabled)

- **Step 5 (Phase 6 closure audit)**: ✅ DONE — exit-gate verification pass
  - Added allocation-stability regression:
    - `ElectricalSubsolver.ReservedScratchBuffersStayStableAcrossSteps`
    - verifies reserved scratch-buffer capacities remain stable across repeated
      solves (guard against per-frame reallocation regressions)
  - Revalidated specialization fallback boundaries:
    - `SolveCountersTrackSpecializedPaths`
    - `SolveCountersTrackDensePathForN3`
  - Performed closure-matrix run (parity + production-path + codegen suites)
    and confirmed all enabled tests pass
  - Exit gate status:
    - ✅ zero per-frame heap allocation regressions detected on reserved path
    - ✅ zero runtime string/hash lookups in AOT electrical solve hot path
      (static arrays + generated bindings/debug maps)
    - ✅ no dead transitional compatibility path remains in active codegen
      hot paths touched during migration

---

## Must-Have vs Nice-to-Have Matrix

Must-have:

- semantic freeze doc + parity fixtures
- generated static plan consumed at runtime
- debug diagnostics for parity/failures
- stable symbolic bindings
- explicit fallback-boundary and test migration tracking
- specialization budget caps

Nice-to-have:

- aggressive small-N unrolled kernels
- richer generated source maps
- optional high-detail debug traces in non-debug configurations

---

## Metrics and Gates

Track continuously:

- parity:
  - max abs/rel voltage error (target: < 1e-5 relative for shared solver path)
  - max abs/rel branch-current error (target: < 1e-5 relative)
  - KCL residual per island (target: < 1e-10 absolute)
- performance:
  - AOT electrical step avg/p95 (baseline established in Phase 0)
  - allocations/frame in electrical solve (target: zero after Phase 1)
  - indirect branch count in electrical solve path (target: zero after Phase 6)
- codegen/build cost:
  - codegen time (must not exceed 2× current codegen time)
  - compile time delta (must not exceed 20% growth from electrical plan addition)
  - generated LOC (tracked, no hard cap until Phase 5)
  - binary size delta (must not exceed 10% from electrical plan addition)
- test migration:
  - count of tests still using raw builder path
  - count relying on classname fallback

---

## Red Flags (Stop / Re-Scope Triggers)

Stop and re-scope if any occurs:

1. JIT/AOT parity failures rise without clear diagnostic attribution
2. IR/schema changes churn across multiple consecutive patches
3. specialization increases compile/binary cost without real workload speedup
4. binding correctness depends on incidental element ordering
5. new tests are added to fallback path instead of production path
6. generated-kernel failures cannot be diagnosed from test output/logs
7. backend-specific numerics introduced before shared-kernel parity stability

---

## First Concrete Coding Milestone

Implement this first (Phase 0 + Phase 1 bootstrap):

1. Write electrical semantics spec (`knowledge/22_electrical_semantics.md`)
   covering the 6 topics listed in Phase 0. This is a documentation-only step.

2. Eliminate per-island heap allocations in `solve_electrical()`:
   - move `island_nodes`, `is_fixed`, `node_to_unknown`, `island_voltages`,
     `fixed_nodes`, `fixed_voltages` into `ElectricalRuntimeState` as
     reusable scratch buffers sized to max island node count
   - this is prerequisite for meaningful performance measurement

3. Add 3-5 mandatory JIT↔AOT parity test fixtures (Phase 0 fixtures)
   that run the same topology through both paths and assert matching results.

4. Add electrical plan emission to `codegen.cpp`:
   - emit `static const ElectricalElement` arrays per island
   - emit `static const uint32_t` signal index arrays per island
   - emit `solve_electrical()` call in generated step method at correct phase position

Rationale:

- step 1 is zero-risk and unblocks parity fixture design
- step 2 removes the most obvious performance floor before measuring anything
- step 3 creates the safety net for all subsequent work
- step 4 is the core deliverable that surfaces codegen/IR/binding issues early

Do NOT combine steps 2 and 4 in one patch. The allocation fix should land
independently so its correctness is verified before codegen changes.

---

## Open Decisions

1. specialization cutoff policy (island size/topology thresholds)
2. numeric policy for specialization paths (identical vs bounded-drift)
3. explicit timeline for reducing classname fallback
4. debug introspection level in release AOT builds
5. `solve_electrical()` interface for static data: span-based vs adapter
   (see Phase 1 key risk — prefer span-based but don't block on it)

## Technical Risks

1. **Codegen complexity jump**: current codegen is ~900 LOC of straightforward
   string emission. Adding electrical plan awareness (island extraction, element
   serialization, phase-correct call placement) may double that. Consider whether
   a separate `electrical_codegen.cpp` is warranted to keep files focused.

2. **Solver interface mismatch**: `ElectricalBuildPlan` uses `std::vector` members.
   Static AOT arrays are `constexpr` / `static const`. Bridging these without
   runtime copies requires either a template/span-based solver interface or
   init-time construction of the plan struct from static data. This design
   decision should be made explicitly before coding Phase 1.

3. **Phase ordering in generated code**: the JIT simulator has a 9-phase step
   (`simulator.cpp`). Generated code currently has a flat `execute()` loop.
   Replicating the correct phase ordering (passive electrical → solve → observers
   → logical → commit → actuators → second solve → logical → sub-rate → finalize)
   in codegen requires careful mapping. A mismatch here would cause subtle
   JIT/AOT divergence that parity fixtures might not catch if fixtures are
   too simple.
