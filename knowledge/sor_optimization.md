# SOR Optimization Guide

Practical tuning guide for this repository.

Goal:

- stable simulation for MSFS systems
- predictable behavior on imperfect schematics
- no over-engineering for high precision

Relevant code:

- `src/jit_solver/SOR_constants.h`
- `src/jit_solver/simulator.h`
- `src/jit_solver/simulator.cpp`
- `src/jit_solver/state.h`
- `src/jit_solver/state.cpp`

## Current solver model (important)

The runtime step is explicit phase-based and uses two electrical solves per outer step:

1. passive electrical stamp
2. first SOR pass
3. observers + logical + control commit
4. actuator electrical stamp
5. second SOR pass
6. sub-rate domains (accumulated simulation `dt`)
7. `finalize_step()`

SOR uses one relaxation sweep per electrical pass. Keep:

```cpp
constexpr int INNER_SWEEPS = 1;
```

in `src/jit_solver/SOR_constants.h` unless solver pipeline is redesigned.

## What we optimized

### 1. Keep canonical omega constant

```cpp
constexpr float OMEGA = 1.3f;
```

Reason: tests/regressions expect this canonical project value.

### 2. Add adaptive runtime omega

Implemented in `src/jit_solver/simulator.cpp`.

Behavior:

- if convergence error worsens by >5%: reduce `omega` quickly (`*0.90`, floor `1.0`)
- if error improves strongly: recover slowly (`*1.01`, cap `SOR::OMEGA`)

Pseudo-shape:

```cpp
if (err > prev * 1.05f) {
    omega_ = max(1.0f, omega_ * 0.90f);
} else if (err < prev * 0.85f) {
    omega_ = min(SOR::OMEGA, omega_ * 1.01f);
}
```

Reset behavior:

- on `start_from_json()` and `stop()` runtime `omega_` and error history reset

## Why this approach

- low-risk change
- preserves existing component/stamping model
- improves robustness on stiff/problematic topologies
- keeps compatibility with current tests and codegen

## What not to do right now

### 1. Do not switch electrical core to push propagation

Electrical/hydraulic networks here are stamped as implicit coupled systems.

### 2. Do not add strong global regularization as first-line fix

There is already small diagonal conditioning in `state.cpp` (`PARASITIC_G`).

Use topology/component fixes first.

### 3. Do not increase inner sweeps without re-baselining

Current tests and tuning are calibrated for one sweep per electrical pass.
Changing `INNER_SWEEPS` requires re-baselining regressions and component tuning.

## Component-side rules that matter most

1. `solve_*()` should stamp/read only
2. persistent state updates go to `finalize_step()`
3. through devices must use `stamp_two_port(...)`
4. avoid extreme defaults (`conductance`, near-zero resistances)

See: `knowledge/component_authoring.md`

## Validation priorities

Most useful stability improvements are schema/topology checks:

1. dangling series device detection (`CurrentSense`, switches, relays, valves)
2. near-short source path warning
3. near-floating node warning

These catch real user graph errors earlier than numeric tweaks.

## Tuning defaults (current)

Use:

```cpp
// src/jit_solver/SOR_constants.h
constexpr float OMEGA = 1.3f;
constexpr int INNER_SWEEPS = 1;
```

Runtime adaptation handles instability by temporarily lowering `omega_`.

## Future upgrade path (if needed)

If you later want multiple sweeps, do it correctly:

- redesign solve loop to re-stamp inside each inner iteration
- then retune `OMEGA` and rebaseline tests

Until then, adaptive `omega` + better validation gives best ROI.
