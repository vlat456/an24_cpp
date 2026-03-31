# Electrical Semantics Specification

Date: 2026-03-31
Status: Phase 0 deliverable — semantic freeze

## Overview

This document defines the canonical electrical model semantics implemented by the
electrical subsolver (`solve_electrical()` in `electrical_subsolver.cpp`). Both JIT
and AOT execution paths share this model. Divergences constitute bugs.

---

## 1. Branch Current Direction Conventions

### Sign Convention

Current is defined as flowing **from node_a to node_b** through a branch element.
A positive current value means this direction; negative means the opposite.

### Element Types and Current Meaning

| Element Kind | Current Calculation |
|--------------|---------------------|
| `ConductanceBranch` | `I = g * (V_a - V_b)` |
| `TheveninSource` | `I = g * (V_a - V_b) - I_n` where `I_n = V_th * g` (Norton equivalent) |
| `FixedVoltageNode` | Current is always 0 (voltage-defined node, not a circuit branch) |

### Branch Current Storage

Branch currents are stored in `ElectricalRuntimeState::branch_currents` indexed by
`element.component_index`. The vector is resized to `max_component_index + 1` per
frame and zero-filled before solving.

### Wrapper Component Bindings

Components that expose branch currents (`Battery`, `Generator`, `CurrentSense`,
`IndicatorLight`) bind to a specific `ElectricalPrimitiveHandle`:

```
handle.component_index → branch_currents[handle.component_index]
```

The handle is assigned during island extraction when the wrapper's solver_role
metadata is processed.

---

## 2. Source Polarity Rules

### TheveninSource (Norton Equivalent)

- `value_a` = Thevenin voltage `V_th` (volts)
- `value_b` = series resistance `R_series` (ohms)
- Internally converted: `g = 1 / max(R_series, 1e-6)`, `I_n = V_th * g`
- The Norton current source injects `-I_n` into `node_a` and `+I_n` into `node_b`
- Net branch current (node_a → node_b): `I = g * (V_a - V_b) - I_n`

### FixedVoltageNode

- `value_a` = fixed voltage at `node_a` (volts)
- `node_b` is unused (set to `UINT32_MAX` sentinel, ignored by solver)
- Defines a Dirichlet boundary condition: that node is held at the specified voltage
- Multiple `FixedVoltageNode` elements on the same node must agree within 1e-5V;
  disagreement throws `std::runtime_error`

### ConductanceBranch

- `value_a` = conductance `g` (siemens = 1/ohm)
- `value_b` is unused (set to 0)
- Must be non-negative; negative conductance throws `std::runtime_error`

---

## 3. Fixed-Node Behavior

### Definition

A node is **fixed** if at least one `FixedVoltageNode` element references it.
All other nodes in an island are **unknown** (solved for).

### Matrix Construction

- Unknown nodes are assigned dense indices 0..N-1 for the linear system `A*x = b`
- Fixed nodes are excluded from the matrix; their voltages are substituted directly
  into stamp equations via the `fixed_voltages` array

### Stamp Rules (Conductance Only)

The following table describes conductance stamping. TheveninSource elements also
inject Norton currents into the RHS per §2 (in addition to conductance stamps).

| Node A | Node B | Stamp Effect |
|--------|--------|--------------|
| unknown | unknown | 4-matrix conductance stamps; no RHS injection |
| unknown | fixed | G-matrix diagonal stamp on unknown; `g * V_fixed` injected into RHS |
| fixed | unknown | Same as above (symmetric) |
| fixed | fixed | No matrix stamp needed (both sides known) |

---

## 4. Singular / Ill-Conditioned Island Behavior

### Singular Detection

After partial pivoting, if the maximum absolute pivot value is less than `1e-12`,
the matrix is considered singular and `solve_dense_gaussian` throws
`std::runtime_error("Singular matrix in electrical solve")`.

### Editor-Friendly Fallback

The singular-matrix exception is caught **inside `solve_electrical()` itself** (not
by the caller). The function never propagates singular-matrix exceptions. Instead:

- The solver sets an internal `solve_ok = false` flag
- Unknown node voltages retain their **previous frame values** (no change)
- All branch currents are zeroed

This keeps the editor/runtime stable when users construct malformed topologies
during live editing.

### AOT Path (Planned)

Both JIT and AOT currently share the same `solve_electrical()` function, which
never propagates singular exceptions. If a future AOT-only solver variant is
introduced that omits the try/catch for performance, generated code must ensure
well-formed islands only (electrical plan generated from validated blueprints).

### Ill-Conditioned (Near-Singular)

Near-singular matrices (small pivots but above threshold) produce numerically
unreliable results. No special handling exists currently. Users must ensure
reasonable resistance values in constructed circuits.

---

## 5. Pivot / Threshold Policy

### Pivot Threshold

`1e-12f` for singularity detection in `solve_dense_gaussian`.

### Series Resistance Floor

`TheveninSource` applies `R_safe = max(R_series, 1e-6f)` before computing
conductance. This prevents division by zero and limits maximum conductance to
`1e6` siemens.

### Conductance Validation

`ConductanceBranch` rejects negative conductance values (throws). Zero
conductance is allowed (open circuit, no stamp).

---

## 6. Float / Double Precision Rules

### Internal Computation

`float` (32-bit IEEE 754) is used exclusively in the solver:
- Matrix coefficients (`A`), RHS (`b`), solution vector, and all voltages/currents
- Gaussian elimination uses `float` arithmetic throughout

### Accumulator Precision

`ElectricalRuntimeState::branch_currents` and `scratch_matrix`/`scratch_rhs`
are `std::vector<float>`. No double-precision accumulation occurs.

### KCL Residual Target

The target KCL residual for pass/fail is `< 1e-10` absolute (per the refinement
plan metrics). This is measured externally via parity fixtures, not enforced
internally by the solver.

---

## 7. Island Extraction (Build-Time)

### Process

1. Collect all electrical signals from components with `Domain::Electrical`
2. Build connectivity graph from `Connection` objects and port aliases
3. Run union-find to partition signals into islands
4. For each island:
   - Extract `FixedVoltageNode` elements from `RefNode` components
   - Extract `TheveninSource` and `ConductanceBranch` elements from primitives
   - Assign `component_index` for branch-current binding

### Signal Index Space

Island `signal_indices` are raw signal indices (from `SimulationState::values`),
not dense node indices. They are sorted and deduplicated when building the
per-island node list for matrix construction.

---

## 8. Phase Ordering

The electrical solve is the **first operation** in the per-frame `step()` function:

### Current JIT Implementation (`simulator.cpp`)

```
Phase 1: solve_electrical (passive electrical: node voltages + branch currents)
Phase 2: scheduler.step  (sources execute → consumers execute → sources commit → consumers commit)
Phase 3: commit_solver_owned_devices (Battery discharge, etc.)
```

The scheduler's `step()` executes all sources, then all consumers (topologically
ordered), then commits all sources, then commits all consumers. There is currently
**no second electrical pass** and **no sub-rate domain dispatch** in the JIT path.

### Target Architecture (for codegen alignment)

The planned full pipeline (from `errors_TODO.md` item 13) is:

```
1. Passive electrical stamp
2. First electrical pass (solve_electrical)
3. Electrical observers
4. Logical pass 1 (feeds actuators)
5. Control commit
6. Actuator stamp + second electrical pass
7. Logical pass 2 (reads converged actuator outputs)
8. Sub-rate domains (mechanical/hydraulic/thermal)
9. Finalize
```

**Note:** The JIT runtime currently collapses phases 2–9 into a single
`scheduler.step()` call. Codegen should target the expanded pipeline above for
correctness. Any JIT/AOT divergence in phase ordering must be documented and
tested via parity fixtures.

Electrical solve must complete before any component `execute()` reads electrical
signals. The generated AOT code must respect this ordering.

---

## 9. Codegen Implications

### What Codegen Must Emit

1. `static const ElectricalElement` arrays per island
2. `static const uint32_t` signal index arrays per island
3. `static const` island count
4. A call to `solve_electrical()` (or its AOT variant) at the correct phase position

### What Codegen Must NOT Do

- Must not emit string/hash lookups in the solve path
- Must not use `std::vector` in the hot path (use `static const` arrays with span-based
  or adapter interface)
- Must not assume iterative solver (push model uses direct solve)

---

## 10. Exit Gate Criteria

For Phase 0, the following must hold:

- [x] This document exists and is committed
- [ ] Every convention above has at least one parity fixture exercising it
- [ ] Baseline benchmark command exists and produces reproducible numbers

---

## Open Issues

None documented. This is the semantic freeze baseline.
