# Mixed-Domain Subsolver Implementation Plan

**Date:** 2026-03-30
**Status:** phases 1-9 implemented, steps 10-15 complete (see implementation notes below)
**Primary driver:** `closed_circuit.blueprint` shows that closed electrical networks cannot be modeled correctly by pure push propagation.

### Implementation Status (All Steps Complete)

All phases through Step 15 are implemented and reviewed:

- commit(dt) migration: done (Phase 1)
- electrical island extraction: done (Phase 2)
- handle plumbing: done (Phase 5)
- local electrical solver: done (Phase 6)
- solver ownership integration: done (Phase 8)
- current sense + battery discharge: done (Batches 6/11)
- pointer lifecycle + real blueprint runaway regression: done (Batch 7)
- first primitive electrical nodes (ElectricalConductance, ElectricalSource): done (Step 13)
- metadata-driven solver role extraction: done (Step 14)
- transitional cleanup + documentation: done (Step 15)

The electrical subsolver MVP is complete. See `knowledge/19_subsolver_step_by_step_plan.md`
for detailed per-step notes.

---

## Goal

Add domain-specific subsolvers to the push runtime, starting with **electrical**, without bringing back the old global iterative solver.

The target architecture is:

- push runtime remains the default execution model
- domains that require network equilibrium get a **local subsolver**
- a component may participate in more than one domain
- the first concrete implementation is an **electrical subsolver**
- the design must be reusable later for **hydraulic** and **mechanical** subsolvers
- the long-term modeling style is **primitive-first composition**, not large privileged domain components

---

## Why This Is Needed

The current push runtime works well for:

- logical propagation
- state machines
- one-way control flow
- many observer/actuator patterns

It does not work for closed physical networks where branch values are coupled.

`closed_circuit.blueprint` demonstrates the failure mode clearly:

- battery adds `v_nominal` to its input each frame
- resistor copies voltage instead of constraining current/voltage
- indicator light copies voltage back into the reference node
- the loop feeds back into the next frame

This is not a one-off bug. It is a modeling boundary. A closed physical network needs a local equilibrium solve.

---

## Non-Goals

This plan does **not** reintroduce:

- the old whole-engine solver
- a global multi-domain matrix spanning every component in the sim
- free-form per-component solver code paths with implicit behavior

This plan does **not** require hydraulic or mechanical subsolvers immediately. It only ensures the electrical implementation does not block them later.

This plan also does **not** assume that `Battery`, `Generator`, `IndicatorLight`, or future aircraft systems remain monolithic forever. The design should allow those to be decomposed into primitive solver-owned and push-owned nodes later.

---

## Core Design Rules

### 1. Push remains the default

If a domain can be modeled as direct propagation, keep using push execution.

### 2. Subsolvers are local to one domain and one connected island

Each subsolver only sees the ports and components of its own domain, partitioned into connected islands.

### 3. Mixed-domain components are allowed

A component may:

- contribute an electrical branch to the electrical subsolver
- still run normal `execute()`/`commit()` logic for logical/mechanical/hydraulic/thermal behavior

Example:

- `IndicatorLight` participates in electrical solve for current/voltage drop
- the same component still computes `brightness` from the solved electrical state

### 3a. Prefer primitive-first domain modeling

The near-term codebase may still contain components such as `Battery` and `Generator`, but the architecture should move toward **primitive nodes**.

For the electrical domain, the solver should ultimately operate on primitives such as:

- fixed reference node
- voltage source / controlled voltage source
- current source
- conductance branch
- ideal switch / topology gate
- probe / measurement node
- accumulator/state node outside the electrical solve

This matters because future systems are expected to be assembled from base-level nodes rather than a few large black-box components.

### 3b. Avoid privileged monolithic physics components

If a future battery is built from:

- an accumulator/state primitive
- math nodes (`Add`, `Multiply`, clamps, LUTs)
- a source primitive
- an internal-resistance branch

then the runtime should support that composition directly.

So the electrical subsolver should be designed around **primitive electrical roles**, not around hardcoded special treatment for a handful of large component classes.

### 4. Solved quantities become the source of truth

For a solved domain, components must not keep writing competing approximations into shared signals.

### 5. Metadata should become the long-term source of truth

The MVP may start with explicit builder wiring for a small set of components, but the target state is metadata-driven classification of subsolver participation.

The long-term metadata should describe **solver roles of primitives**, not just component classnames.

---

## Target Runtime Architecture

## Runtime Flow

At a high level, one `step(dt)` should become:

1. run pre-solve control/state updates that affect source parameters
2. solve electrical islands
3. run electrical observers that read solved electrical state
4. run logical/control phases that depend on solved electrical values
5. run other domain phases
6. commit stateful components with `dt`

The exact phase ordering may be tuned, but the critical change is:

- **electrical branch voltages/currents come from the electrical subsolver, not from per-component pass-through writes**

## New Concepts

### Domain island

A connected subgraph inside one physical domain.

Examples:

- one DC electrical net
- one hydraulic line network
- one mechanical shaft network

### Domain build plan

Build-time data extracted from the blueprint for a specific subsolver.

### Domain runtime state

Per-step scratch buffers and solved results for one subsolver.

---

## Electrical Subsolver MVP

Start with a narrow supported set:

- `RefNode`
- `Battery`
- `Generator`
- `Resistor`
- `IndicatorLight`
- `Load` once given a real electrical branch model
- `CurrentSense` as a reader of solved current, not as a pseudo-solver element

This component list is only the **bootstrap surface** for the current repo state.

The intended direction is to express these behaviors as a smaller set of electrical primitives. For example:

- `Battery` should eventually be decomposable into a source primitive plus state/math primitives
- `IndicatorLight` should eventually be decomposable into a conductance primitive plus visual/brightness logic

### Electrical quantities to solve

The electrical solver must produce at least:

- node voltages for electrical signals
- branch current per electrical element that needs it

The solver does **not** need to solve every domain. Only electrical islands.

### Element models for MVP

Use a compact, explicit element set:

1. **FixedVoltageNode**
   - provided by `RefNode`
   - clamps a node to a fixed voltage

2. **TheveninSource**
   - provided by `Battery` and `Generator`
   - positive node, negative/reference node, open-circuit voltage, series resistance

3. **ConductanceBranch**
   - provided by `Resistor`
   - later also by `IndicatorLight` and simple electrical loads
   - two nodes plus conductance

4. **CurrentProbe**
   - not a solver element initially
   - `CurrentSense` reads current already solved for the branch it monitors

This is enough to fix the closed battery-resistor-indicator-refnode loop.

### Primitive-first reinterpretation of current components

To avoid over-investing in monolithic component behavior, the MVP should treat current components as temporary wrappers over primitive solver roles:

| Current component | MVP electrical role | Longer-term direction |
|---|---|---|
| `RefNode` | fixed reference primitive | keep as primitive |
| `Battery` | source + series resistance + optional state | split into source primitive + accumulator/math nodes |
| `Generator` | source + series resistance | split into source primitive + control/math nodes |
| `Resistor` | conductance primitive | keep as primitive |
| `IndicatorLight` | conductance primitive + derived brightness | split into conductance primitive + observer/visual node |
| `CurrentSense` | observer/probe | keep as primitive |

This keeps the MVP practical without locking the architecture to big component classes.

---

## Concrete Code Changes

## 1. Add subsolver build/runtime types

### New files

- `src/jit_solver/subsolvers/electrical_subsolver.h`
- `src/jit_solver/subsolvers/electrical_subsolver.cpp`
- `src/jit_solver/subsolvers/subsolver_types.h`

### New core structs

Suggested starting point:

```cpp
enum class ElectricalElementKind {
    FixedVoltageNode,
    TheveninSource,
    ConductanceBranch,
};

struct ElectricalElement {
    ElectricalElementKind kind;
    uint32_t node_a = 0;
    uint32_t node_b = 0;
    float value_a = 0.0f;
    float value_b = 0.0f;
    uint32_t component_index = UINT32_MAX;
};

struct ElectricalIslandPlan {
    std::vector<uint32_t> signal_indices;
    std::vector<ElectricalElement> elements;
};

struct ElectricalBuildPlan {
    std::vector<ElectricalIslandPlan> islands;
};

struct ElectricalRuntimeState {
    std::vector<float> branch_currents;
    std::vector<float> scratch_matrix;
    std::vector<float> scratch_rhs;
};
```

If useful, add one more explicit layer to make the primitive direction obvious:

```cpp
struct ElectricalPrimitiveHandle {
    uint32_t island_index = UINT32_MAX;
    uint32_t element_index = UINT32_MAX;
};
```

Wrapper components can keep one of these handles until they are eventually replaced by direct primitive nodes.

The exact fields can change, but the builder must be able to:

- identify which electrical signals belong to each island
- enumerate solver elements inside that island
- map solved results back to components/signals

---

## 2. Extend `BuildResult`

### File

- `src/jit_solver/jit_solver.h`

Add an electrical build plan to `BuildResult`.

Suggested direction:

```cpp
struct BuildResult {
    ...
    ElectricalBuildPlan electrical_plan;
};
```

Do not put heavy scratch buffers into `BuildResult`; keep runtime scratch in the solver/runtime side.

---

## 3. Extract electrical islands during build

### File

- `src/jit_solver/jit_solver.cpp`

### Work

After existing port-to-signal mapping is available:

1. Identify components that participate in the electrical subsolver MVP.
2. Read their electrical ports from existing provider/metadata setup.
3. Build a graph of electrical nodes and elements.
4. Partition into connected electrical islands.
5. Store the result into `BuildResult::electrical_plan`.

For the MVP, this extraction may still be classname-driven for the small bootstrap set. But structure the code so the extracted result is a list of primitive electrical roles, not a list of special component cases.

### Important rule

Do not let components owned by the electrical subsolver also remain active as electrical writers in the push scheduler.

Specifically for MVP:

- `Battery`, `Generator`, `RefNode`, `Resistor` must stop using their current electrical `execute()` writes once the electrical solver owns them
- `IndicatorLight` should stop writing electrical `v_out`, but may still run for `brightness`

This avoids double-applying incompatible models.

Longer-term, the cleaner end state is not merely “disable push writes on these big components”, but “replace those big components with primitive solver-owned and push-owned nodes”.

---

## 4. Update runtime step order

### Files

- `src/jit_solver/simulator.cpp`
- possibly `src/jit_solver/scheduler.h`

### Required change

Insert the electrical subsolver into the main step.

Suggested order for MVP:

1. pre-solve source/control updates
2. electrical subsolver solve
3. electrical observers / derived outputs
4. logical phases
5. slower domains
6. commit with `dt`

The exact split between step 1 and step 3 depends on which components currently stage state in `execute()` vs `commit()`.

### Commit signature change

The current `commit()` API does not receive `dt`, but battery discharge needs it.

Change scheduler commit callback from:

```cpp
void commit(SimulationState& st)
```

to:

```cpp
void commit(SimulationState& st, float dt)
```

### Files affected

- `src/jit_solver/scheduler.h`
- all components with `commit()` methods

This is a mechanical change and is cleaner than hiding `dt` in component state.

---

## 5. Rework component responsibilities

This section describes the MVP in terms of current repository components. It is intentionally transitional.

The target design is primitive-first composition.

### Battery

Files:

- `src/jit_solver/components/battery.h`
- `src/jit_solver/components/battery.cpp`

New role:

- contribute a `TheveninSource` element to the electrical plan
- stop computing `v_out = v_in + v_nominal` directly in runtime electrical path
- use solved branch current in `commit(st, dt)` to decrease `charge`

Longer-term direction:

- split `Battery` into a source primitive plus state/math primitives
- move state-of-charge logic out of the hardcoded electrical source implementation where possible

MVP battery behavior:

- `v_nominal` is open-circuit voltage
- `internal_r` is series resistance
- `charge` decreases from solved current
- no SoC voltage curve yet unless needed later

### Generator

Same as Battery, minus charge depletion.

Longer-term direction:

- source primitive plus control/math composition

### RefNode

New role:

- contribute a fixed-voltage constraint to the electrical plan
- stop acting as a plain write source inside electrical solve path

This is already close to a good primitive and should probably remain primitive.

### Resistor

New role:

- contribute a conductance branch
- stop writing `v_out = v_in`

This is a core primitive and should remain one.

### IndicatorLight

New role:

- contribute a conductance branch electrically
- compute brightness from solved voltage across the branch
- stop writing electrical pass-through voltage

Longer-term direction:

- split into an electrical conductance primitive and a visual observer/brightness node

### Load

Current `Load` is electrically a no-op.

Before moving it into the electrical solver, define what it is physically:

- fixed conductance load
- fixed power load
- switched load

For MVP, only migrate `Load` if it is defined as a simple conductance branch.

If `Load` becomes a family of composable base nodes later, avoid re-encoding those behaviors into a new monolithic solver-side load type.

### CurrentSense

New role:

- read solved current from the branch it monitors
- remove the current fake formula `i_out = (v_in - v_out) * conductance` for solved electrical paths

This should become a permanent primitive observer.

---

## 6. Decide where solved current lives

Recommendation:

- keep solved node voltages in `SimulationState::values`
- store solved branch currents in `ElectricalRuntimeState`
- give components a stable way to retrieve their branch current by handle/index

Do **not** add a generic `through[]` back immediately unless there is a clear domain-wide need.

Reason:

- the immediate need is solved branch current for electrical elements
- `through[]` suggests a broader domain abstraction that has not yet been designed for mixed-domain subsolvers
- branch result storage is enough for the MVP and easier to reason about

If later hydraulic/mechanical subsolvers need analogous per-branch solved values, introduce a shared pattern then.

This also fits the primitive-first direction better than inventing large component-specific output contracts.

---

## 7. Metadata plan

### MVP

It is acceptable to start with explicit builder wiring in `jit_solver.cpp` for the MVP element set.

### Target

Move toward explicit metadata for subsolver participation.

The important update is this: metadata should eventually describe **primitive solver roles** and **observer roles**, so a composed battery or generator does not require special runtime classification logic.

Suggested future metadata shape:

```json
{
  "domains": ["Electrical", "Hydraulic"],
  "subsolver_roles": {
    "Electrical": "ConductanceBranch",
    "Hydraulic": "None"
  }
}
```

Possible future extension:

```json
{
  "subsolver_roles": {
    "Electrical": {
      "kind": "TheveninSource",
      "ports": {"pos": "v_out", "neg": "v_in"},
      "params": {"voltage": "v_nominal", "series_r": "internal_r"}
    }
  }
}
```

Or, for a primitive conductance node:

```json
{
  "subsolver_roles": {
    "Electrical": {
      "kind": "ConductanceBranch",
      "ports": {"a": "v_in", "b": "v_out"},
      "params": {"g": "conductance"}
    }
  }
}
```

Exact schema can wait. The point is to describe primitive roles, not hand-maintain a list of privileged classes forever.

This should come after the electrical MVP proves the runtime shape.

Do not block the MVP on a full metadata redesign.

---

## Implementation Phases

## Phase 1: Infrastructure skeleton

### Deliverables

- new `subsolvers/` directory
- `ElectricalBuildPlan` added to `BuildResult`
- empty electrical solver callable from simulator
- `commit(st, dt)` signature migrated

### Verification

- project builds
- existing push tests still pass after mechanical `commit` API migration
- electrical solver can be invoked with no-op behavior

---

## Phase 2: Build electrical islands

### Deliverables

- builder extracts electrical islands for MVP components
- debug logging or assertions show island composition
- components under electrical ownership are no longer scheduled as push electrical writers
- internal representation is already phrased in primitive solver roles, even if source components are still legacy wrappers

### Verification

- build-time tests for island extraction
- one test verifying `closed_circuit.blueprint` creates a single electrical island with expected nodes/elements

---

## Phase 3: Solve node voltages for MVP elements

### Deliverables

- electrical solver handles fixed nodes, Thevenin sources, and conductance branches
- solved node voltages written back to `SimulationState::values`
- runaway loop in `closed_circuit.blueprint` is eliminated

### Verification

- `ClosedCircuit_NoRunawayVoltage`
- `RefNode_ClampsNode`
- `BatteryAndResistor_ProducesFiniteCurrent`

---

## Phase 4: Migrate derived components

### Deliverables

- `IndicatorLight` brightness derived from solved voltage drop
- `CurrentSense` reads solved current
- `Load` either migrated with explicit branch model or left out until modeled

Stretch goal within this phase if cheap:

- introduce one or two explicit primitive electrical node types in the library/runtime so new blueprints can start composing behavior without relying on monolithic wrappers

### Verification

- `IndicatorLight_BrightnessUsesSolvedVoltage`
- `CurrentSense_ReadsSolvedCurrent`

---

## Phase 5: Battery discharge

### Deliverables

- battery uses solved current in `commit(st, dt)`
- `charge` decreases over time under load
- battery remains stable at zero-charge boundaries

### Verification

- `Battery_DischargeUsesSolvedCurrent`
- `Battery_ChargeClampedToRange`

---

## Phase 6: Metadata hardening

### Deliverables

- explicit metadata or generated registry support for subsolver roles
- builder classification no longer depends on handwritten classname lists where avoidable
- primitive electrical node types can be described without new hardcoded runtime cases

### Verification

- metadata validation tests
- negative tests for missing/invalid subsolver role metadata once the new schema is active

---

## File-Level Checklist

## New files

- `src/jit_solver/subsolvers/subsolver_types.h`
- `src/jit_solver/subsolvers/electrical_subsolver.h`
- `src/jit_solver/subsolvers/electrical_subsolver.cpp`
- `tests/test_electrical_subsolver.cpp`
- `tests/test_electrical_island_build.cpp`

## Existing files to modify

- `src/jit_solver/jit_solver.h`
- `src/jit_solver/jit_solver.cpp`
- `src/jit_solver/scheduler.h`
- `src/jit_solver/simulator.cpp`
- `src/jit_solver/components/battery.h`
- `src/jit_solver/components/battery.cpp`
- `src/jit_solver/components/generator.h`
- `src/jit_solver/components/generator.cpp`
- `src/jit_solver/components/ref_node.h`
- `src/jit_solver/components/ref_node.cpp`
- `src/jit_solver/components/resistor.h`
- `src/jit_solver/components/resistor.cpp`
- `src/jit_solver/components/indicator_light.h`
- `src/jit_solver/components/indicator_light.cpp`
- `src/jit_solver/components/current_sense.h`
- `src/jit_solver/components/current_sense.cpp`
- `src/jit_solver/components/load.h`
- `src/jit_solver/components/load.cpp`
- `tests/CMakeLists.txt`

## Likely new primitive/library follow-up files

These are not required for MVP, but the plan should leave room for them:

- `library/electrical/*.blueprint` primitive node definitions
- primitive source/conductance/probe node implementations if introduced as first-class components
- generated metadata/registry files that encode primitive subsolver roles

## Docs to update after implementation

- `knowledge/02_simulation.md`
- `knowledge/03_components.md`
- `knowledge/16_push_migration_plan.md`
- `knowledge/push_improvement.md`

---

## Testing Plan

## Build-time tests

1. electrical island extraction from a simple closed loop
2. island extraction with two disconnected electrical nets
3. mixed-domain component stays in non-electrical scheduler buckets while contributing electrical solver element(s)
4. primitive-role extraction works the same whether behavior comes from a wrapper component or a future primitive node

## Runtime regression tests

1. `ClosedCircuit_NoRunawayVoltage`
2. `Resistor_UsesConductance`
3. `Battery_UsesInternalResistance`
4. `CurrentSense_ReadsSolvedCurrent`
5. `Battery_DischargeUsesSolvedCurrent`
6. composed source/conductance graphs behave the same as equivalent wrapper-based graphs once primitive nodes exist

## Safety tests

1. unsupported electrical element inside a solved island fails fast
2. duplicated fixed-voltage constraints on one node fail fast with clear error
3. missing branch handle for a solved observer component fails fast in debug builds

---

## Risks And Mitigations

## Risk 1: Double ownership of electrical behavior

If a component both contributes to the electrical subsolver and keeps writing electrical outputs in push execution, results will be inconsistent.

### Mitigation

- explicitly disable push electrical writes for subsolver-owned component classes
- add tests that solved components do not also run electrical pass-through behavior

## Risk 2: Over-generalizing too early

Trying to design the final cross-domain abstraction before shipping electrical MVP will slow progress.

### Mitigation

- implement electrical first with clean but narrow types
- extract common domain-subsolver abstractions only after one real implementation exists
- support primitive-first composition conceptually, but do not block MVP on immediately replacing all large components

## Risk 3: Metadata churn blocks delivery

Strict metadata is desirable, but a full schema redesign can delay the real fix.

### Mitigation

- use explicit builder wiring for MVP
- add metadata-driven classification in a follow-up hardening phase

## Risk 4: `commit(dt)` API migration touches many files

### Mitigation

- do the signature change first as a dedicated mechanical patch
- keep behavior unchanged for components that ignore `dt`

---

## Recommended Execution Order

1. migrate `commit()` to `commit(st, dt)`
2. add empty electrical subsolver plumbing
3. build electrical island extraction
4. stop scheduling electrical pass-through writes for solver-owned components
5. solve node voltages for `RefNode`, `Battery`, `Generator`, `Resistor`
6. migrate `IndicatorLight`
7. migrate `CurrentSense`
8. implement battery discharge from solved current
9. harden metadata and validation
10. start introducing primitive electrical node types so wrapper components can shrink over time

This order gets the unstable closed-circuit bug under control early while keeping each step reviewable.

---

## Acceptance Criteria

The plan is complete when all of these are true:

1. `closed_circuit.blueprint` no longer exhibits runaway voltage
2. resistor conductance affects solved voltage/current
3. battery internal resistance affects terminal voltage under load
4. battery charge decreases from solved current over time
5. indicator brightness and current sensing read solved electrical state
6. push runtime remains the default for non-solved domains
7. architecture can add a future hydraulic subsolver without revisiting the electrical design from scratch
8. architecture supports gradual replacement of monolithic electrical components with composed primitive nodes

---

## Final Recommendation

Implement the electrical subsolver as a **local domain island solver**, not as a scheduler patch and not as a return to the old global solver.

That gives a clean path to:

- fix the current electrical correctness bugs
- support mixed-domain components cleanly
- add hydraulic/mechanical subsolvers later using the same build-plan/runtime-state pattern
- move the project toward primitive-first composed systems instead of growing new privileged monolithic electrical components

Electrical should be the first real implementation. Generalization should happen only after that implementation proves the pattern. Primitive composition should be the long-term direction, but MVP should stay narrow and practical.
