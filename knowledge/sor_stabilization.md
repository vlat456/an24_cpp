# SOR Stabilization Plan (MSFS Scale)

## Goal

Make electrical simulation robust for large mixed-domain blueprints (target up to ~2000 components) without over-engineering for scientific precision.

Success criteria for game use:

- stable and deterministic behavior (no random phase-order artifacts)
- convergence to plausible values for broad topology combinations
- bounded startup transients and no sustained control-loop oscillations unless intentionally modeled
- acceptable runtime at 60 Hz on typical player hardware

## Scope and Non-Goals

In scope:

- electrical solver stability and scheduling robustness
- domain-bridge behavior (`electrical <-> logical`)
- JIT/AOT parity
- diagnostics for bad graphs and risky parameter sets

Out of scope:

- high-precision circuit simulation (SPICE-grade)
- replacing electrical nodal solve with pure push propagation

Rationale: push propagation is fine for acyclic logical signals, but not for coupled electrical networks with loops, source contention, and bidirectional effects.

## Mandatory Active-Phase Rules

These rules are non-negotiable during current active refactor phase:

- no inference code for execution behavior (zero tolerance)
- no legacy compatibility paths
- no fallback behavior when declarations are missing
- if declarations are incomplete/invalid, fail fast (load/build error)

All execution behavior must be explicitly declared in JSON/schema metadata.

Practical meaning:

- remove classname/heuristic inference for phase/domain traits
- remove implicit defaults that silently guess execution hooks
- require every device/type definition to provide explicit execution declarations
- accept breakage during migration; fixing declarations is part of rollout

Migration policy for this phase:

- correctness and explicitness over backward compatibility
- hard-break old assets that rely on implicit behavior
- do not add temporary compatibility shims that preserve inference

## Current Baseline (as of this plan)

- SOR uses re-stamp-per-inner-sweep (`INNER_SWEEPS = 4`)
- two electrical passes exist in step pipeline
- actuator stamping is present in both electrical passes
- adaptive omega is active for JIT

Known weakness:

- phase semantics are still convention-driven and can be fragile for new component combinations
- some control loops need manual tuning due to mixed bridge behavior and saturations

## Target Architecture (Pragmatic, Not Academic)

### 1) Keep hybrid model

- electrical/hydraulic networks: nodal/stamp + iterative solve
- logical/control graph: push/read-write signal propagation in logical phase

### 2) Make scheduler contracts explicit

Per-step phase contract:

1. electrical pass A: stamp passive + actuator using previous committed command
2. SOR A (re-stamp each inner sweep)
3. electrical observers (read solved state only)
4. logical solve
5. control commit
6. electrical pass B: restamp passive + actuator using new command
7. SOR B (re-stamp each inner sweep)
8. sub-rate domain ticks (mechanical/hydraulic/thermal)
9. finalize

No component may silently rely on undefined ordering outside this contract.

### 3) Formalize component execution traits

Each component type must declare explicit hooks/capabilities instead of relying on inference:

- stamps in pass A, pass B, or both
- observer hook requirement
- logical hook requirement
- commit hook requirement
- sub-rate domain participation

Invalid or partial trait definitions should fail load/build, not fallback.

Source of truth requirement:

- execution traits are declared in JSON/type definitions
- runtime/codegen consume declarations only
- C++ side may validate declarations but must not infer missing behavior

## Stabilization Workstreams

## WS1 - Scheduler Hardening

Deliverables:

- single scheduler specification shared by JIT and AOT
- generated/executed order parity tests
- remove ad-hoc branching in per-phase loops where possible
- remove all inference/fallback scheduling paths

Hard requirements:

- scheduler build fails if any component lacks explicit JSON execution traits
- JIT and AOT both read the same explicit declarations

Acceptance:

- JIT/AOT phase traces match for same blueprint
- no control-loop latency regressions in existing tests

## WS2 - Numerical Guardrails

Deliverables:

- centralized solver knobs in `src/jit_solver/SOR_constants.h`
- per-step convergence telemetry (max delta, optional residual estimate)
- non-convergence watchdog with bounded fallback behavior (log + clamp strategy)

Defaults (game-oriented):

- keep `OMEGA` conservative
- keep finite `INNER_SWEEPS`
- prioritize bounded behavior over perfect convergence

Acceptance:

- no NaN/Inf propagation in stress tests
- deterministic replay under fixed seed/input

## WS3 - Topology and Parameter Validation

Deliverables:

- warnings/errors for problematic graphs:
  - dangling series devices
  - near-floating nodes
  - near-short source paths
  - missing reference node paths
- warnings for risky component parameters:
  - extreme conductance values
  - contradictory controller limits/gains

Acceptance:

- invalid graphs are rejected or loudly diagnosed before runtime instability
- undeclared execution traits are rejected with hard errors

## WS4 - Control-Loop Robustness Patterns

Deliverables:

- reusable loop tuning guidance for bridge chains (`VoltageSense -> PI/PID -> LUT/filters -> actuator`)
- startup behavior policy (bias, ramps, anti-windup discipline)
- reference tuned blueprints for common regulator patterns

Acceptance:

- reference loops settle within target time and ripple bounds
- no sustained oscillation in standard regression scenarios

## WS5 - Scale Performance (2000 Components)

Deliverables:

- electrical graph partitioning into connected islands
- solve only active islands each step
- lightweight profiling counters by phase and domain

Acceptance:

- 60 Hz budget holds on representative large scenes
- worst-case spikes are bounded and diagnosable

## Test Plan (Must-Have)

### Determinism and Parity

- JIT vs AOT equivalence on same blueprint + dt stream
- fixed-dt vs variable-dt sanity envelopes

### Topology Sweep

- generated/randomized mixed topologies (bounded parameter ranges)
- assert no crashes, no NaN/Inf, bounded voltages/currents

### Control Loop Regression

- regulator blueprints with expected settle windows and ripple limits
- startup transient assertions (max overshoot, settle time)

### Existing Regression Coverage

Keep green at minimum:

- `SorRegression.*`
- `CurrentSense.*`
- `SwitchRegression.SOR_ConvergesWithLoad`
- `GS24Regression.*`
- `CodegenAccumulator.*` scheduler/phase checks

## Implementation Phases

Phase 0 (already started):

- re-stamp-per-sweep implemented
- two-pass electrical behavior aligned in JIT/AOT

Phase 1:

- codify and enforce execution traits
- lock phase contract tests

Phase 2:

- add topology/parameter validators
- add convergence watchdog and telemetry

Phase 3:

- island partitioning + performance instrumentation
- large-scene profiling and tuning

Phase 4:

- control-loop reference templates and documented tuning bands

## Risk Register

- Hidden phase coupling in legacy components -> mitigate with trait audit + contract tests
- JIT/AOT drift -> mitigate with generated phase-trace comparison tests
- Over-tuning one flagship loop (GSC) while breaking others -> mitigate with loop matrix regressions
- Performance regressions from stricter checks -> mitigate with debug/diagnostic build flags

## Operational Rules for Agents

- Do not replace electrical SOR with push propagation.
- Keep all solver constants centralized in `src/jit_solver/SOR_constants.h`.
- Maintain JIT/AOT parity for any scheduler or solver change.
- Zero inference rule: do not add any execution inference code.
- Zero legacy rule: do not add compatibility fallbacks/shims.
- Require JSON-declared execution traits for all components; fail fast if missing.
- For controller tuning, report settle time, overshoot, and steady-state ripple numerically.

## Exit Criteria

This plan is complete when all are true:

- stable execution for representative mixed-domain scenes up to target scale
- deterministic JIT/AOT parity on regression suite
- validated phase contract with no hidden dependencies
- bounded startup and steady-state control behavior for core regulator blueprints
- acceptable 60 Hz runtime budget with diagnostics for overload cases
