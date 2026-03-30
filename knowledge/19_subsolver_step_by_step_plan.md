# Subsolver Step-By-Step Plan

**Date:** 2026-03-30
**Status:** implementation checklist
**Companion docs:**

- `knowledge/push_improvement.md`
- `knowledge/18_subsolver_implementation_plan.md`

---

## Current Implementation Status

### Steps 1-9: COMPLETED (Batches 1-9)

- [x] Step 1: Regression tests (current failure captured)
- [x] Step 2: commit(dt) migration
- [x] Step 3: Empty electrical subsolver skeleton
- [x] Step 4: Build-time electrical island extraction
- [x] Step 5: Runtime handles from wrapper components to solved elements
- [x] Step 6: Minimal electrical solver for one island (TheveninSource + ConductanceBranch + FixedVoltageNode)
- [x] Step 7: Write solved voltages back into SimulationState::values
- [x] Step 8: Remove double ownership of electrical writes
- [x] Step 9: IndicatorLight derived-from-solve brightness

### Steps 10-11: COMPLETED

- [x] Step 10: CurrentSense reads solved current (not fake formula)
- [x] Step 11: Battery discharge from solved current

### Step 12: COMPLETED

- [x] Step 12: Integration test for closed_circuit.blueprint

  **Real fixture test**: `ClosedCircuitBlueprint_BatteryChargeDecreases_RealFixture`
  loads actual `closed_circuit.blueprint` (battery_1→resistor_1→indicatorlight_1→bus_1→refnode_1),
  runs 10 000 steps, and asserts monotonic charge decrease and measurable total discharge.

  **Controlled topology companion**: `ClosedCircuitLike_BatteryChargeDecreases_CorrectedTopology`
  uses a higher-conductance inline topology for stronger discharge signal.

  **Root cause of prior flakiness (resolved)**:
  Battery charge was stored as `float`. At charge=1000.0f, the float32 ULP is ~6.1e-5.
  With the real blueprint's 10Ω resistor (conductance=0.1), the per-step discharge delta
  was ~1.2e-5 Ah — below the ULP, so subtractions were silently swallowed by rounding.
  Fix: `charge` and `capacity` are now `double` (ULP ~1.1e-13 at 1000.0). This is the
  standard engineering practice for running accumulators in simulation.

  **Existing no-runaway test**: `ClosedCircuitBlueprint_NoRunawayVoltage` continues
  to verify voltage stability for the same fixture.

### Step 13: COMPLETED

- [x] Step 13: First explicit primitive electrical nodes

  **Two new primitives created:**
  - `ElectricalConductance` — maps directly to `ConductanceBranch` solver role (ports: v_in, v_out; param: conductance)
  - `ElectricalSource` — maps directly to `TheveninSource` solver role (ports: v_in, v_out; params: voltage, resistance)

  **Full pipeline wiring:** library blueprints, C++ headers/impls, `all.h`, regenerated `port_registry.h` (76 components),
  builder Phase 2 cases, `is_solver_owned_electrical_propagator()`, electrical island extraction, `commit_solver_owned_devices()`.

  **10 verification tests** in `test_electrical_primitives.cpp`:
  1. Wrapper Resistor and primitive ElectricalConductance produce equivalent solve results
  2. Primitive-only circuit (ElectricalSource + ElectricalConductance + RefNode) solves correctly
  3. Primitive-only circuit stable over 500 steps (no drift)
  4. Build plan correctly includes primitive elements with correct kinds and parameters
  5. Primitives are solver-owned (not push-scheduled)
  6. Mixed wrapper + primitive in same island solves correctly
  7. ElectricalSource and Battery produce equivalent results
  8. Two conductances in series solve correctly
  9. Default parameters work correctly
  10. Unknown params on primitives throw (validates strict param checking)

### Steps 14-15: COMPLETED

- [x] Step 14: Minimal metadata for primitive solver roles
- [x] Step 15: Clean up transitional wrapper logic

  **SolverRole schema added:**
  - `kind`: "ConductanceBranch" | "TheveninSource" | "FixedVoltageNode"
  - `ports`: role-key → port-name mapping (e.g., `{"a": "v_in", "b": "v_out"}`)
  - `params`: role-key → param-name mapping (e.g., `{"g": "conductance"}`)

  **Metadata added to 3 primitive blueprints:**
  - `ElectricalConductance.blueprint` → ConductanceBranch
  - `ElectricalSource.blueprint` → TheveninSource
  - `RefNode.blueprint` → FixedVoltageNode

  **Dual-path electrical extraction (jit_solver.cpp):**
  - Path 1 (metadata-driven): When `dev.solver_role.has_value()`, uses `resolve_role_port()` and
    `read_role_param()` to extract elements generically from the role metadata. Continues past
    classname fallback.
  - Path 2 (classname fallback): Remains for all component types, both wrappers (Battery,
    Generator, Resistor, IndicatorLight, CurrentSense) and primitives (RefNode,
    ElectricalConductance, ElectricalSource). This fallback is **intentionally retained** because
    `build_systems_dev()` is used directly by many tests without library loading, so `solver_role`
    is never populated in those paths.

  **Key discovery:** `build_systems_dev()` does NOT call `merge_device_instance()`. Tests that
  create `DeviceInstance` objects directly never go through the library loading path, so
  `solver_role` is never populated. Classname-based fallback for ALL types must remain.

  **4 new metadata validation tests:**
  1. `MetadataProducesCorrectElementKind` — validates all 3 kinds produce correct elements
  2. `MetadataMissingPortKeyThrows` — missing required port keys fail clearly
  3. `MetadataMissingParamKeyThrows` — missing required param keys fail clearly
  4. `MetadataPropagatedThroughLibraryPipeline` — end-to-end through `start_from_json()`

  **Intentionally remaining transitional paths:**
  - Battery → TheveninSource (classname-based, until decomposed into source primitive + state nodes)
  - Generator → TheveninSource (classname-based, until decomposed)
  - Resistor → ConductanceBranch (classname-based, until metadata added to its blueprint)
  - IndicatorLight → ConductanceBranch (classname-based, until decomposed)
  - CurrentSense → ConductanceBranch (classname-based, until decomposed)
  - RefNode → FixedVoltageNode (classname fallback retained for direct `build_systems_dev()` usage)
  - ElectricalConductance → ConductanceBranch (classname fallback retained for same reason)
  - ElectricalSource → TheveninSource (classname fallback retained for same reason)

---

## Purpose

This document turns the mixed-domain subsolver direction into a concrete execution sequence.

It is intentionally detailed and conservative:

- small reviewable steps
- clear file targets
- explicit verification after each step
- minimal over-generalization in the MVP

The MVP target remains:

- **electrical only**
- **DC only**
- **resistive only**
- **primitive-first design direction**
- current large components treated as temporary wrappers where needed

---

## Final MVP Outcome

At the end of this plan, the codebase should be able to:

1. build an electrical island from a blueprint
2. solve node voltages for a small set of electrical primitives
3. expose solved branch current to readers
4. stop runaway voltage in `closed_circuit.blueprint`
5. make resistor conductance matter
6. make battery internal resistance matter
7. make battery charge decrease from solved current

---

## Scope Boundaries

## In scope

- `RefNode`
- `Battery`
- `Generator`
- `Resistor`
- `IndicatorLight`
- `CurrentSense` as observer
- optional simple `Load` only if modeled as conductance

## Out of scope for MVP

- AC behavior
- capacitors/inductors
- non-resistive transient electrical elements
- full metadata redesign before first working implementation
- hydraulic/mechanical subsolvers
- removing all wrapper components immediately

---

## Work Strategy

Each step should satisfy one rule:

**At every point in the sequence, either behavior is unchanged, or the new behavior is protected by focused tests.**

Do not combine these in one patch unless trivial:

- API migration
- builder refactor
- runtime solver insertion
- component semantic rewrites
- metadata redesign

---

## Step 1: Freeze Current Failure With Regression Tests

### Goal

Capture the current electrical failure in tests before changing architecture.

### Files

- `tests/` new or existing push/runtime regression suite
- possibly a new fixture loading `closed_circuit.blueprint`

### Work

Add tests that demonstrate current broken behavior without baking in the broken behavior as desired behavior.

Recommended tests:

1. `ClosedCircuit_RunawayVoltageObserved`
   - load `closed_circuit.blueprint`
   - run a small number of frames
   - assert voltage increases monotonically on the reference loop in current runtime
   - mark as temporary characterization test if needed

2. `Resistor_ConductanceCurrentlyUnused`
   - build two equivalent blueprints with different resistor conductance
   - assert output voltage is identical today

3. `Battery_ChargeCurrentlyStatic`
   - run with apparent load path
   - assert battery charge does not change today

### Notes

These tests may later be deleted or inverted once the new behavior exists. Their purpose is to lock down the diagnosis.

### Verification

- tests compile
- characterization tests pass on current runtime

---

## Step 2: Migrate `commit()` API To Carry `dt`

### Goal

Prepare for battery discharge and any future state integration that depends on frame time.

### Files

- `src/jit_solver/scheduler.h`
- all component headers/implementations with `commit()`
- any AOT/JIT glue that assumes old signature
- tests that call helpers wrapping `commit()` or stepping

### Work

Change commit function shape from:

```cpp
void commit(SimulationState& st)
```

to:

```cpp
void commit(SimulationState& st, float dt)
```

### Rules

- keep behavior unchanged for all existing components
- components that do not need `dt` should ignore it explicitly
- do not mix electrical solver work into this patch

### Verification

- full project builds
- targeted push runtime tests pass
- no behavior change in existing suites except signature adaptation

---

## Step 3: Add Empty Electrical Subsolver Skeleton

### Goal

Create the code structure without changing behavior.

### Files to add

- `src/jit_solver/subsolvers/subsolver_types.h`
- `src/jit_solver/subsolvers/electrical_subsolver.h`
- `src/jit_solver/subsolvers/electrical_subsolver.cpp`

### Files to edit

- `src/jit_solver/jit_solver.h`
- `src/jit_solver/simulator.cpp`

### Work

Add minimal types:

- `ElectricalElementKind`
- `ElectricalElement`
- `ElectricalIslandPlan`
- `ElectricalBuildPlan`
- `ElectricalRuntimeState`

Add `electrical_plan` to `BuildResult`.

Add a no-op electrical solver call into `Simulator::step()`.

### Rules

- no real solving yet
- runtime output must stay unchanged

### Verification

- build passes
- runtime behavior unchanged
- no-op subsolver can be called safely every frame

---

## Step 4: Add Build-Time Electrical Role Extraction

### Goal

Teach the builder to recognize electrical solver-owned roles for the MVP bootstrap set.

### Files

- `src/jit_solver/jit_solver.cpp`
- possibly helper declarations in `jit_solver.h` or new private builder helpers

### Work

After signal mapping is built:

1. identify solver-participating components in the MVP set
2. convert them to primitive electrical roles
3. record node/signal indices and element parameters
4. build connected electrical islands
5. save the plan into `BuildResult::electrical_plan`

### Primitive roles for bootstrap phase

- `RefNode` -> `FixedVoltageNode`
- `Battery` -> `TheveninSource`
- `Generator` -> `TheveninSource`
- `Resistor` -> `ConductanceBranch`
- `IndicatorLight` -> `ConductanceBranch` plus later observer behavior

### Important implementation constraint

The builder output must already describe **primitive roles**, even if detection is still classname-based for the MVP.

### Verification

Add tests:

1. `ElectricalIslandBuild_ClosedCircuit_SingleIsland`
2. `ElectricalIslandBuild_TwoDisconnectedNets`
3. `ElectricalIslandBuild_WrapperMapsToPrimitiveRole`

Each test should inspect the build plan, not runtime solved values.

---

## Step 5: Add Runtime Handles From Wrapper Components To Solved Elements

### Goal

Allow current wrapper components to retrieve their solved branch results later.

### Files

- `src/jit_solver/subsolvers/subsolver_types.h`
- current component headers where needed:
  - `battery.h`
  - `generator.h`
  - `indicator_light.h`
  - `current_sense.h`

### Work

Add a small handle type, for example:

```cpp
struct ElectricalPrimitiveHandle {
    uint32_t island_index = UINT32_MAX;
    uint32_t element_index = UINT32_MAX;
};
```

Assign this handle during build for components that need to query solved current/voltage later.

### Rules

- do not yet change component execution semantics
- just wire the lookup path

### Verification

- build plan tests confirm stable handles are assigned where expected
- components still run as before

---

## Step 6: Implement Minimal Electrical Solver For One Island

### Goal

Solve node voltages for the primitive set:

- fixed voltage node
- Thevenin source
- conductance branch

### Files

- `src/jit_solver/subsolvers/electrical_subsolver.cpp`
- `src/jit_solver/subsolvers/electrical_subsolver.h`

### Work

Implement a compact local solver per electrical island.

Requirements for MVP:

1. solve node voltages for each island
2. handle at least one fixed node per island
3. support source with series resistance
4. support conductance branches
5. compute branch current for each source/branch element

### Important design constraint

Prefer a direct small solve over open-ended runtime iteration.

This is the point of the MVP:

- local
- bounded
- small element set
- predictable cost

### Fail-fast requirements

If an island contains unsupported electrical roles in the solve path:

- fail during build, or
- fail at runtime with explicit error in debug/developer mode

Do not silently fall back to wrong physics.

### Verification

Add tests in `tests/test_electrical_subsolver.cpp` for:

1. one fixed node + one source + one resistor
2. one source + two resistors in series
3. one source + two parallel branches
4. duplicate incompatible fixed constraints rejected

These should test the solver directly, not full simulator integration yet.

---

## Step 7: Write Solved Voltages Back Into `SimulationState::values`

### Goal

Make solved electrical node voltages visible to the rest of the runtime.

### Files

- `src/jit_solver/simulator.cpp`
- `src/jit_solver/subsolvers/electrical_subsolver.cpp`

### Work

After solving an island:

- map solved node voltages back to the corresponding electrical signal indices
- write them into `SimulationState::values`

### Rules

- only electrical-solver-owned signals should be written from this solve path
- do not let later push electrical pass-through components overwrite these values

### Verification

Add a simulator-level test:

- `ElectricalSolve_WritesBackNodeVoltages`

---

## Step 8: Remove Double Ownership Of Electrical Writes

### Goal

Prevent components under electrical solver ownership from continuing to write incompatible push electrical values.

### Files

- `src/jit_solver/jit_solver.cpp`
- `src/jit_solver/scheduler.h`
- component files if needed for observer-only execution path split

### Work

For solver-owned electrical components:

- stop scheduling their old electrical write behavior into the push electrical path
- keep only non-electrical derived behavior where needed

For example:

- `Battery` no longer writes `v_out = v_in + v_nominal`
- `Resistor` no longer writes `v_out = v_in`
- `IndicatorLight` no longer writes electrical pass-through voltage
- `RefNode` no longer acts as a plain source write if the subsolver owns that node

### Suggested implementation pattern

For wrapper components, split responsibilities conceptually into:

- solver contribution at build time
- post-solve derived output calculation at runtime

### Verification

Add or update tests:

1. `ClosedCircuit_NoRunawayVoltage`
2. `SolvedElectricalComponents_DoNotPassThroughWrite`

This is the step where the runaway bug should first disappear.

---

## Step 9: Migrate `IndicatorLight` To Derived-From-Solve Behavior

### Goal

Make the light compute brightness from solved electrical state rather than from pass-through voltage propagation.

### Files

- `src/jit_solver/components/indicator_light.h`
- `src/jit_solver/components/indicator_light.cpp`

### Work

Change `IndicatorLight` runtime behavior to:

- retrieve solved voltage difference across its branch
- compute brightness from that solved drop or terminal voltage
- stop writing electrical transport outputs

### Verification

Add tests:

1. `IndicatorLight_BrightnessUsesSolvedVoltage`
2. `IndicatorLight_NoElectricalPassThrough`

---

## Step 10: Migrate `CurrentSense` To Read Solved Current

### Goal

Make current sensing authoritative.

### Files

- `src/jit_solver/components/current_sense.h`
- `src/jit_solver/components/current_sense.cpp`

### Work

Remove the pseudo-formula:

```cpp
i_out = (v_in - v_out) * conductance;
```

Replace it with:

- lookup solved current from the branch or monitored primitive handle
- write that result to `i_out`

### Open design point

You may need a way for `CurrentSense` to monitor:

- its own primitive branch, or
- an adjacent branch by explicit binding

For MVP, prefer the simplest path supported by current blueprints.

### Verification

Add tests:

1. `CurrentSense_ReadsSolvedCurrent`
2. `CurrentSense_DiffersFromOldFakeFormula` if helpful as a characterization/inversion test

---

## Step 11: Implement Battery Discharge From Solved Current

### Goal

Fix the battery state update using authoritative solved current.

### Files

- `src/jit_solver/components/battery.h`
- `src/jit_solver/components/battery.cpp`

### Work

In `commit(st, dt)`:

1. read solved branch current through battery source handle
2. convert current and `dt` to charge delta
3. subtract from `charge`
4. clamp to valid range

Keep MVP battery model minimal:

- use `v_nominal` and `internal_r`
- discharge from current
- defer SoC-to-OCV curve unless explicitly needed

### Verification

Add tests:

1. `Battery_DischargeUsesSolvedCurrent`
2. `Battery_ChargeClampedToRange`
3. `Battery_NoDischargeWithoutLoad`

---

## Step 12: Add Integration Test For `closed_circuit.blueprint`

### Goal

Verify that the original motivating bug is fixed end-to-end.

### Files

- regression/integration tests under `tests/`

### Work

Add a full simulator test loading `closed_circuit.blueprint`.

Assertions should initially be conservative:

1. no runaway voltage after many steps
2. reference node remains fixed
3. battery output remains finite
4. branch current is finite
5. battery charge decreases if the branch draws current

Do not hardcode precise final numbers unless all participating branch models are explicitly defined and stable.

### Verification

- integration test passes in JIT path
- if practical, add AOT parity later as a follow-up

---

## Step 13: Introduce First Explicit Primitive Electrical Nodes

### Goal

Start moving away from monolithic wrapper components toward the future architecture.

### Files

- new component implementations for one or two primitive nodes
- library definitions under `library/`
- generated metadata if required by current pipeline

### Recommended first primitives

1. electrical conductance primitive
2. electrical source primitive or fixed reference primitive

### Work

Create at least one new primitive electrical node that maps directly to a solver role without semantic wrapper logic.

This step proves that the architecture supports composed systems, not only retrofitted legacy wrappers.

### Verification

Add tests:

1. wrapper-based resistor and primitive resistor produce equivalent solve results
2. primitive-only simple circuit solves correctly

---

## Step 14: Add Minimal Metadata For Primitive Solver Roles

### Goal

Reduce dependence on handwritten builder classification.

### Files

- metadata schema / parser paths
- generated registry files if needed
- `src/jit_solver/jit_solver.cpp`

### Work

Add just enough metadata to describe primitive solver participation, for example:

- domain
- solver role kind
- participating ports
- required params

Do not redesign all metadata at once. Only support what the primitive electrical MVP needs.

### Verification

Add tests:

1. primitive node with valid solver metadata builds into correct primitive role
2. missing required role metadata fails fast
3. invalid port mapping fails fast

---

## Step 15: Clean Up Transitional Wrapper Logic

### Goal

Document and reduce temporary compatibility paths introduced during MVP.

### Files

- `jit_solver.cpp`
- wrapper component files
- knowledge docs

### Work

Identify all transitional decisions, for example:

- classname-based wrapper mapping to primitive roles
- legacy wrapper-only special handling
- duplicated role logic between code and metadata

Then:

- simplify what can now be removed
- document what remains intentionally transitional

### Verification

- no dead electrical pass-through logic remains for solver-owned classes
- docs updated

---

## Recommended Commit Boundaries

Use small commits aligned with architecture boundaries.

Suggested sequence:

1. tests capturing current failure and `commit(dt)` migration
2. empty subsolver skeleton
3. build-time electrical island extraction
4. primitive handles and direct solver unit tests
5. real island voltage solve
6. writeback to state + disable double electrical ownership
7. migrate indicator/current sense
8. battery discharge
9. `closed_circuit.blueprint` integration regression
10. first primitive node(s)
11. metadata hardening

---

## Stop Conditions

Pause and reassess if any of these happen:

1. the electrical solver starts needing unsupported element types just to pass the first integration test
2. component wrappers require too much bespoke translation logic
3. metadata redesign grows larger than the solver work itself
4. AOT integration becomes more complex than JIT MVP by an order of magnitude

If that happens, shrink scope again:

- solve fewer component types
- add fail-fast validation for unsupported graphs
- postpone primitive library rollout until after stable JIT behavior exists

---

## Acceptance Checklist

Mark the MVP done only when all are true:

- electrical islands are built explicitly
- local electrical solve runs every frame
- solver-owned components no longer perform electrical pass-through writes
- `closed_circuit.blueprint` is stable
- resistor conductance changes circuit behavior
- battery current is solved, not guessed
- battery charge changes from solved current
- at least one explicit primitive electrical node exists or the architecture path for it is proven with tests

---

## Immediate Next Action

If implementation starts now, the best first coding step is:

**Step 2 + Step 3 in sequence:**

1. migrate `commit()` to `commit(st, dt)`
2. add empty electrical subsolver plumbing

That creates the extension points needed for the rest of the work without changing runtime behavior yet.
