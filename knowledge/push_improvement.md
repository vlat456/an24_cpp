# Push Runtime Improvement Notes

**Date:** 2026-03-30
**Subject:** `closed_circuit.blueprint` exposes hard limits of the current push runtime for electrical networks.
**Status:** analysis complete; implementation strategy updated.

---

## Executive Summary

The first version of this note correctly identified the three user-visible symptoms, but it overstated one root cause and recommended one intermediate fix that is not strong enough.

The confirmed findings are:

| # | Symptom | Immediate cause | Deeper cause |
|---|---|---|---|
| A | No voltage drop after resistor | `Resistor::execute()` copies `v_in` to `v_out` | The runtime does not solve passive networks |
| B | Voltage grows by +28V every frame | The loop writes back into the battery reference node | The runtime allows closed electrical feedback without a network solve |
| C | Battery does not discharge | `Battery::commit()` is empty | There is no authoritative load/current model for sources |

The important correction is this:

- The bug is **not primarily “source ordering”**.
- Ordering affects the exact transient, but even with `RefNode` executed before `Battery`, the closed loop is still structurally wrong because passive components are implemented as pass-through writers and can feed a source reference node from the previous pass.
- The deeper issue is that the current push runtime treats electrical components as value propagators, while a real electrical loop requires solving constraints over a connected network.

The best solution is **not** to patch the scheduler with special restore rules for fixed signals.

The best solution is:

1. Keep the push runtime for control, logic, state machines, and one-way signal propagation.
2. Add a **small dedicated electrical subsolver** for connected electrical nets containing ideal/fixed voltage sources and passive two-terminal elements.
3. Make Battery/Generator/RefNode/Resistor/IndicatorLight/Load participate in that electrical solve instead of writing directly into shared voltage signals as pass-through components.

This is the smallest architecture that can actually fix all three issues without introducing fragile special cases.

---

## Scope Of Analysis

Files inspected during analysis:

- `closed_circuit.blueprint`
- `src/jit_solver/scheduler.h`
- `src/jit_solver/simulator.cpp`
- `src/jit_solver/state.h`
- `src/jit_solver/jit_solver.cpp`
- `src/jit_solver/components/battery.cpp`
- `src/jit_solver/components/generator.cpp`
- `src/jit_solver/components/ref_node.cpp`
- `src/jit_solver/components/resistor.cpp`
- `src/jit_solver/components/indicator_light.cpp`
- `src/jit_solver/components/load.cpp`
- `src/jit_solver/components/current_sense.cpp`

---

## Test Circuit Topology

`closed_circuit.blueprint` builds this loop:

```text
RefNode(0V) -> Battery(v_in)
Battery(v_out) -> Resistor(v_in)
Resistor(v_out) -> IndicatorLight(v_in)
IndicatorLight(v_out) -> Bus -> Battery(v_in) / RefNode(v)
```

After union-find port collapsing in `build_systems_dev()`, the electrical graph has three signal groups:

| Signal | Ports |
|---|---|
| A | `indicatorlight_1.v_out`, `bus_1.v`, `battery_1.v_in`, `refnode_1.v` |
| B | `battery_1.v_out`, `resistor_1.v_in` |
| C | `resistor_1.v_out`, `indicatorlight_1.v_in` |

So the battery is modeled as a source from A to B, resistor from B to C, indicator from C to A, and refnode clamps A to 0.

That is a real closed loop. Closed loops need a network solve or an equivalent formulation. The current runtime does not provide one.

---

## Confirmed Behavior In Code

### Battery

`src/jit_solver/components/battery.cpp`

```cpp
float v_in = st.values[provider.get(PortNames::v_in)];
st.values[provider.get(PortNames::v_out)] = v_in + v_nominal;
```

Battery behavior today is:

- read one signal
- add `v_nominal`
- write another signal

It does **not**:

- use `internal_r`
- compute terminal current
- reduce `charge`
- enforce any source/load equilibrium

`Generator` uses the same pattern.

### Resistor

`src/jit_solver/components/resistor.cpp`

```cpp
float v_in = st.values[provider.get(PortNames::v_in)];
st.values[provider.get(PortNames::v_out)] = v_in;
```

Resistor behavior today is pure pass-through. `conductance` is loaded but unused.

### IndicatorLight

`src/jit_solver/components/indicator_light.cpp`

```cpp
float v_in = st.values[provider.get(PortNames::v_in)];
st.values[provider.get(PortNames::brightness)] = ...;
st.values[provider.get(PortNames::v_out)] = v_in;
```

IndicatorLight is also a pass-through electrical element. It derives brightness from voltage, but electrically it behaves like an ideal wire.

### Load

`src/jit_solver/components/load.cpp`

`Load` is electrically a no-op. It reads nothing meaningful and writes nothing.

### CurrentSense

`src/jit_solver/components/current_sense.cpp`

```cpp
float v_diff = v_in - v_out;
st.values[provider.get(PortNames::i_out)] = v_diff * conductance;
```

CurrentSense does not read actual network current. It estimates current from voltage difference and a local conductance parameter. That only makes sense if node voltages are already physically meaningful. In the current runtime they are often not.

---

## Issue A: Why There Is No Voltage Drop After The Resistor

This part of the original note was correct.

### Direct cause

`Resistor::execute()` sets:

```cpp
v_out = v_in;
```

So there is no possible drop across the resistor regardless of the configured resistance.

### Why this is fundamental

A passive two-terminal component needs one of these models:

1. A network solve that determines both node voltages from all connected elements.
2. An explicit causal model with a known downstream load and a formula for the resulting drop.

The current implementation has neither. It only copies a value forward.

### Important correction to the earlier draft

The previous draft proposed adding `through[]` and still keeping `st.values[v_out] = v_in` inside the resistor. That is internally inconsistent.

If the resistor continues to write `v_out = v_in`, then the voltage difference across the resistor is immediately forced to zero by construction, and any derived current becomes artificial rather than solved.

So “add current accumulation but keep pass-through voltage propagation” is not a real resistor model.

---

## Issue B: Why Voltage Skyrockets

### What happens now

With the current scheduler:

1. sources run
2. consumers run
3. values remain in `SimulationState::values` for the next frame

In this blueprint:

- Battery reads signal A and writes B = A + 28
- Resistor copies B to C
- IndicatorLight copies C back to A
- On the next frame Battery reads the previously amplified A again

This creates:

```text
A(n+1) = A(n) + 28V
```

So voltage grows linearly without bound.

### What the earlier draft got partly wrong

The draft said the root cause was source ordering. That is incomplete.

Ordering matters, but it is not the actual architectural defect.

Why:

- If `RefNode` runs after `Battery`, Battery may read stale A from the previous frame.
- If `RefNode` runs before `Battery`, consumers still overwrite the same shared signal later in the step.
- On the next frame, Battery can still read the value produced by the previous consumer pass unless the runtime also protects or re-solves that node.

So the real defect is:

- a closed electrical loop exists
- passive elements are modeled as forward writers, not constraints
- a source input node can be overwritten by downstream propagation
- there is no electrical equilibrium step that re-establishes consistent node voltages

### Why fixed-signal restoration is not the best answer

The earlier draft suggested saving fixed signals before the consumer pass and restoring them afterward.

That can suppress this specific runaway behavior, but it is still the wrong abstraction.

Weak points of that approach:

1. It is a scheduler patch for what is really a modeling problem.
2. It silently discards writes instead of solving the circuit.
3. It only helps nodes explicitly classified as fixed.
4. It does not produce physically correct branch voltages or currents.
5. It risks spreading more hidden special cases into scheduling.

It is acceptable as an emergency guardrail, but not as the target architecture.

---

## Issue C: Why Battery Does Not Discharge

This part of the original note was directionally correct but too ambitious in one respect.

### Direct cause

`Battery::commit()` is empty.

So even if current were known, charge would still never change.

### Deeper cause

The runtime has no authoritative battery current. Therefore it has no trustworthy input for:

- charge depletion
- internal resistance drop
- energy accounting
- thermal side effects later

### Correction to the earlier draft

The earlier draft assumed a battery SoC-to-voltage curve should be part of the immediate fix. That may be reasonable eventually, but it is not required to solve the present bug.

The minimum correct battery fix is:

1. compute branch current from the electrical solve
2. decrement charge from that current
3. optionally expose SoC later for open-circuit-voltage effects

Battery discharge is blocked first by missing current, not by missing SoC curves.

---

## Weak Points In The First Draft

The first draft was useful as a symptom inventory, but the proposed path had several inconsistencies.

### 1. It over-blamed source order

Source order is a contributing detail, not the core problem.

The core problem is that closed electrical loops are being simulated with one-way value propagation.

### 2. The proposed `through[]` model was underspecified

The draft proposed current accumulation without defining how node voltages would be solved consistently.

That leaves unanswered:

- How are `v_in` and `v_out` for a resistor determined in the first place?
- How does a node settle when multiple branches meet?
- What prevents arbitrary overwrite by later components?
- How are ideal voltage sources reconciled with passive branches?

Without that, `through[]` alone is not enough.

### 3. The resistor example was self-contradictory

The draft's resistor pseudo-code both:

- computed current from `v_in - v_out`
- then immediately forced `v_out = v_in`

That collapses the voltage difference to zero and defeats the current model.

### 4. The battery pseudo-code used `dt` inside `commit()` without passing it

Current `commit()` signature is:

```cpp
void commit(SimulationState& st)
```

So the proposed:

```cpp
charge -= i * dt / 3600.0f;
```

does not fit the existing API. Either `dt` must be stored, passed differently, or battery discharge must occur in `execute()` or a redesigned commit hook.

### 5. The expected post-fix voltages were not justified

The draft predicted `~27.97V` after the resistor and `2.8A` current, but the current `IndicatorLight` model does not define a real electrical resistance compatible with that calculation.

So those numbers were illustrative, not derivable from the present component models.

They should not be stated as expected results unless the indicator electrical model is first defined.

### 6. Fixed-signal restoration was presented too favorably

It is a containment patch, not a robust solution.

---

## Best Solution

### Recommendation

Do **not** try to make closed electrical circuits physically correct by layering more scheduler tricks on top of the current push propagation.

Instead, introduce a **dedicated electrical net solve** while keeping the push runtime for everything else.

This is the best tradeoff because it:

- fixes all three issues from one coherent model
- keeps the current push architecture for non-electrical domains
- avoids special-case signal freezing and overwrite rollback
- matches how electrical loops actually behave
- can stay much smaller than the removed legacy global iterative solver

### What “dedicated electrical net solve” means here

Not a return to the old whole-engine legacy solver.

It means:

1. Build connected electrical subgraphs from electrical ports.
2. For each electrical subgraph, assemble a compact local solve for that frame.
3. Support only the subset needed now:
   - fixed voltage reference (`RefNode`)
   - ideal voltage source with series resistance (`Battery`, `Generator`)
   - passive conductance between two nodes (`Resistor`, `IndicatorLight`, possibly `Load` once modeled)
4. Write solved node voltages back into `SimulationState::values`.
5. Expose solved branch currents to components that need them.

This can be done per connected electrical island, not as a full multi-domain global solver.

### Why this is better than `through[]`-only push augmentation

Because voltage and current in a closed electrical network are not independent propagated signals. They are coupled unknowns.

You can accumulate current after voltages are known, or solve voltages and currents together. But you cannot get physically meaningful voltages by copying them through branches and then “backfilling” current afterward.

So if `through[]` is added, it should be an **output of the electrical subsolver**, not a partial substitute for it.

---

## Target Architecture

### Keep as-is conceptually

- Push scheduler for control flow and non-electrical domains
- Union-find signal mapping for non-physical shared signals
- `execute()` / `commit()` pattern for stateful devices
- JIT/AOT component architecture

### Change for electrical domain

Electrical two-terminal components should stop acting like forward voltage writers in closed loops.

Instead:

- `RefNode` contributes a fixed node voltage constraint
- `Battery` contributes source EMF, series resistance, and later charge state
- `Generator` contributes source EMF and series resistance
- `Resistor` contributes a branch conductance between two nodes
- `IndicatorLight` contributes branch conductance plus brightness calculation from solved voltage drop
- `Load` contributes an explicit electrical model, not a no-op
- `CurrentSense` reads solved branch current or solved node current balance

### Minimal electrical state additions

`SimulationState` will likely need:

- solved node voltages
- branch currents or a queryable per-component current result
- maybe cached per-net temporary buffers owned outside `SimulationState`

The exact storage is less important than this rule:

**Current must come from the electrical solve, not from ad hoc component-local voltage-difference guesses.**

---

## Practical Migration Plan

### Phase 0: Immediate Guardrail

Short-term safety patch, if needed before the real work:

- detect electrical loops that include active sources plus pass-through consumers
- fail fast at build time or log a loud validation error

This is better than silently simulating impossible voltages.

If a runtime guardrail is required, fixed-signal restoration is acceptable only as a temporary containment measure and should be documented as such.

### Phase 1: Introduce electrical net extraction

In build:

- identify electrical components that belong to a solvable electrical subset
- partition them into connected electrical islands
- store enough metadata to assemble a local solve per island

This should be explicit metadata-driven classification, not more classname heuristics if avoidable.

### Phase 2: Implement minimal local electrical solver

Support only these elements first:

- `RefNode`
- `Battery`
- `Generator`
- `Resistor`
- `IndicatorLight`

Expected first success criterion:

- `closed_circuit.blueprint` stabilizes to bounded voltages
- resistor voltage drop becomes non-zero when the downstream branch has finite conductance
- battery current becomes queryable

### Phase 3: Battery discharge from solved current

Once branch current is available:

- update battery charge every frame from solved current
- clamp at `[0, capacity]`
- leave SoC-to-OCV refinement for later unless a real requirement appears

### Phase 4: Convert measurement components

- `CurrentSense` should read solved current, not estimate from local `ΔV * G`
- voltmeter-like readers should read solved node difference

### Phase 5: Expand electrical library coverage

Migrate additional electrical components one by one to the electrical subsolver model.

---

## Why Not Return To The Old Global Solver

Because that would reintroduce the cost and complexity that push migration was trying to remove.

The recommendation here is narrower:

- only solve electrical islands
- only for electrical components that actually need network behavior
- keep logic, controllers, hydraulics, thermal, and state machines in the existing push flow

That preserves the main performance and architecture benefits of the push runtime while restoring correctness where pure propagation is insufficient.

---

## Test Strategy

### Required regressions

1. `ClosedCircuit_NoRunawayVoltage`
   Verifies the closed battery-resistor-indicator-refnode loop remains bounded.

2. `Resistor_UsesConductance`
   Verifies changing resistor conductance changes solved node voltages and current.

3. `Battery_UsesInternalResistance`
   Verifies loaded terminal voltage differs from ideal EMF when current flows.

4. `Battery_DischargeUsesSolvedCurrent`
   Verifies charge decreases proportionally to solved current over time.

5. `CurrentSense_ReadsSolvedCurrent`
   Verifies CurrentSense output matches the electrical solve rather than a guessed local formula.

### Validation expectation for `closed_circuit.blueprint`

The exact final voltages should not be hardcoded yet, because `IndicatorLight` does not currently have a proper electrical branch model.

What should be asserted first:

- ground/reference node remains fixed
- no unbounded voltage growth occurs
- solved source current is finite and non-negative under load
- battery charge decreases if the branch model draws current

Only after the indicator electrical model is specified should exact numeric voltage/current assertions be added.

---

## Final Conclusion

`closed_circuit.blueprint` is not exposing three unrelated bugs. It is exposing one architectural boundary:

**The current push runtime cannot correctly model closed electrical networks because it propagates values through components instead of solving electrical constraints.**

The first draft was right that resistor pass-through, battery no-op discharge, and runaway voltage are all real problems. But the strongest corrective path is not:

- reorder sources
- restore fixed signals
- add a loosely defined `through[]`

The strongest path is:

- add a small electrical subsolver for connected electrical nets
- use that solved voltage/current state as the source of truth
- keep the rest of the push runtime intact

That is the most coherent way to fix:

- no voltage drop after resistor
- skyrocketing voltage in closed loops
- battery not discharging

without replacing one fragile approximation with another.

---

## Related Files

- `closed_circuit.blueprint`
- `src/jit_solver/scheduler.h`
- `src/jit_solver/simulator.cpp`
- `src/jit_solver/state.h`
- `src/jit_solver/jit_solver.cpp`
- `src/jit_solver/components/battery.cpp`
- `src/jit_solver/components/generator.cpp`
- `src/jit_solver/components/ref_node.cpp`
- `src/jit_solver/components/resistor.cpp`
- `src/jit_solver/components/indicator_light.cpp`
- `src/jit_solver/components/load.cpp`
- `src/jit_solver/components/current_sense.cpp`
- `knowledge/16_push_migration_plan.md`
- `knowledge/errors_TODO.md`
