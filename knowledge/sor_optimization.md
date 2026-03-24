# SOR Optimization Notes

## Scope

This project uses an electrical-style nodal relaxation solver, not neural-network backprop. Any discussion of `push`, `spectral radius`, `regularization`, or `latency` has to be translated into the actual implementation in:

- `/Users/vladimir/an24_cpp/src/jit_solver/simulator.cpp`
- `/Users/vladimir/an24_cpp/src/jit_solver/state.cpp`
- `/Users/vladimir/an24_cpp/src/jit_solver/state.h`
- `/Users/vladimir/an24_cpp/src/jit_solver/components/all.cpp`
- `/Users/vladimir/an24_cpp/src/jit_solver/SOR_constants.h`

## What the solver actually does today

### 1. This is not a full inner SOR loop

The runtime solver performs:

1. component stamping into `through[]` and `conductance[]`
2. `precompute_inv_conductance()`
3. `save_convergence_state()`
4. one call to `solve_sor_iteration(...)`
5. `post_step()` updates
6. logical-domain solve after electrical state update

Relevant code:

- `/Users/vladimir/an24_cpp/src/jit_solver/simulator.cpp:192`
- `/Users/vladimir/an24_cpp/src/jit_solver/state.h:88`

So current behavior is closer to a real-time relaxed fixed-point update than a traditional "iterate until converged this timestep" solver.

### 2. `omega` is static

`omega_` is initialized from a compile-time constant:

- `/Users/vladimir/an24_cpp/src/jit_solver/simulator.h:90`
- `/Users/vladimir/an24_cpp/src/jit_solver/SOR_constants.h:10`

There is no adaptive `omega`, no line search, and no spectral monitoring.

### 3. There is already implicit regularization

`precompute_inv_conductance()` adds:

```cpp
constexpr float PARASITIC_G = 1e-7f;
float total_g = conductance[i] + PARASITIC_G;
inv_conductance[i] = 1.0f / total_g;
```

Source:

- `/Users/vladimir/an24_cpp/src/jit_solver/state.cpp:46`

This is effectively diagonal regularization. Physically it behaves like a tiny leakage-to-ground term. It prevents division by zero and forces floating nodes to relax toward 0 V.

### 4. The codebase already contains evidence that implicit coupling matters

Several bug-fix comments show that convergence breaks when components mutate hidden state during stamping or when two-port physics is approximated as independent one-port loads.

Examples:

- `RefNode` switched to proper Norton stamping to stop divergence:
  - `/Users/vladimir/an24_cpp/src/jit_solver/components/all.cpp:161`
- `SolenoidValve` had to use `stamp_two_port(...)` instead of two disconnected loads:
  - `/Users/vladimir/an24_cpp/src/jit_solver/components/all.cpp:794`
- `GidroAccumulator` state mutation moved from solve phase to `post_step()` because it destabilized convergence:
  - `/Users/vladimir/an24_cpp/src/jit_solver/components/all.cpp:835`
- `RUG82` integration moved to `post_step()` because doing it inside solve made effective gain depend on iteration count:
  - `/Users/vladimir/an24_cpp/src/jit_solver/components/all.cpp:1008`

These are strong signs that the solver is correctly treating the electrical/hydraulic core as an implicitly coupled system.

## Hypothesis check

### Hypothesis 1: "Need spectral-radius analysis"

Partly true, but with a caveat.

In principle, yes: for a linearized fixed-point update

```text
v_{k+1} = v_k + omega * D^{-1} r(v_k)
```

stability is governed by the spectral radius of the iteration operator near equilibrium. If that radius exceeds 1, the update diverges.

But in this codebase:

- there is no assembled global matrix `A`
- many components are nonlinear or stateful
- only one relaxed step is taken per frame

So a full exact spectral-radius computation is not practical as an online control mechanism.

What is practical:

- estimate local instability from `max_change` growth between steps
- detect sign-flipping / oscillatory nodes
- adapt `omega` downward when residual growth persists

Conclusion:

- **The theory is relevant**
- **Exact online spectral analysis is probably overkill here**
- **Adaptive `omega` based on observed contraction is justified**

### Hypothesis 2: "Replace SOR with PUSH"

For the electrical core: mostly no.

Why:

1. Electrical/hydraulic stamping here is bidirectional and simultaneous.
2. `stamp_two_port(...)` explicitly couples node pairs through shared conductance.
3. Union-find signal allocation and nodal potentials assume a shared equilibrium variable, not message passing.

Push-style propagation works well when the graph is:

- acyclic
- directional
- naturally feed-forward
- not governed by Kirchhoff-style simultaneous constraints

That is **not** the main electrical solve in this project.

Push would be more reasonable for:

- logical/control graphs after electrical solve
- pure signal-processing chains
- topologically sorted subgraphs with no algebraic loops

Conclusion:

- **Do not replace the electrical SOR core with push**
- **A hybrid is plausible**: keep SOR for implicit physical domains, use push/topological evaluation for acyclic logical graphs

Notably, the code already separates logical solve after electrical update:

- `/Users/vladimir/an24_cpp/src/jit_solver/simulator.cpp:213`
- `/Users/vladimir/an24_cpp/src/jit_solver/components/all.cpp:651`

That separation is already a step toward a hybrid architecture.

### Hypothesis 3: "Use LM or L2 regularization instead of a low-pass fix"

Conceptually valid, but only in a very specific sense.

Adding `lambda I` to the diagonal of the linearized system would reduce aggressiveness and improve conditioning. In this solver, that corresponds to increasing effective diagonal conductance.

But this project already does a small version of that via `PARASITIC_G`.

So the real question is not "should regularization exist?" but:

> should the current tiny parasitic diagonal term be replaced with a larger, tunable, or adaptive damping term?

Assessment:

- **Small diagonal regularization is already justified and already implemented**
- **Large LM/L2-style damping would bias the physics**, because it is equivalent to adding artificial leakage
- **It should not be used as a blanket cure for bad topology or unstable component models**

Best use case:

- as a bounded stabilization tool for pathological floating or near-singular nodes
- possibly configurable for debug or fallback modes

Bad use case:

- using it to hide wrong stamps, hidden state mutation during solve, or bad domain partitioning

Conclusion:

- **Mild regularization: justified**
- **Aggressive LM/L2 as main stabilization strategy: not justified for a physics simulator**

## Main architectural conclusion

The strongest problem is not "SOR vs push".

It is this:

> the runtime solver performs only one relaxation step per frame with fixed `omega`, while also simulating nonlinear/stateful components.

That creates a system whose stability depends heavily on:

- component stamping quality
- separation between solve-time stamping and post-step state mutation
- fixed `omega`
- timestep size and subrate accumulation

So the most justified improvements are:

## Recommended changes

### 1. Add adaptive `omega`

This is the most justified next step.

Simple policy:

- track `get_max_change()` per frame
- if error grows for N consecutive frames, reduce `omega`
- if error shrinks consistently, cautiously increase `omega` toward a cap

Example direction:

```cpp
if (err > prev_err * 1.05f) {
    omega_ = std::max(1.0f, omega_ * 0.9f);
} else if (err < prev_err * 0.8f) {
    omega_ = std::min(1.5f, omega_ * 1.02f);
}
```

Why this is justified:

- low implementation cost
- fits current architecture
- directly addresses oscillation/divergence risk
- does not require global matrix assembly

### 2. Keep implicit solve for electrical and hydraulic domains

Do not replace these with push propagation.

The current stamping model encodes simultaneous constraints. Replacing it with push would be a solver rewrite and would likely lose correctness on closed loops and stiff couplings.

### 3. Consider push/topological solve only for acyclic logical graphs

This is the one place where the "make graph acyclic where possible" advice is justified.

Possible future direction:

- detect DAG subgraphs in `Domain::Logical`
- evaluate them in topological order
- keep only cyclic logical groups on iterative evaluation if needed

### 4. Treat diagonal regularization as a safety rail, not the primary fix

`PARASITIC_G` is fine as a tiny conditioning aid.

If instability appears:

1. inspect component stamps
2. ensure hidden state changes happen in `post_step()`, not `solve_*()`
3. tune or adapt `omega`
4. only then consider slightly stronger diagonal damping

## Practical ranking

### Most justified

1. adaptive `omega`
2. better instability diagnostics
3. stronger audit of components that mutate internal state during solve

### Conditionally justified

4. topological push evaluation for acyclic logical-only subgraphs
5. optional debug-mode stronger diagonal damping

### Not justified as primary direction

6. replacing electrical SOR with push
7. aggressive LM/L2 regularization as the main convergence strategy
8. exact online spectral-radius computation for the full nonlinear system

## Suggested diagnostics to add

Useful additions without changing the solver model:

- per-frame residual metric in UI/logs
- per-node oscillation detector
- `omega` history log
- warnings for near-floating nodes where `conductance[i]` is dominated by `PARASITIC_G`

Example near-floating heuristic:

```cpp
if (conductance[i] < 10.0f * PARASITIC_G) {
    // warn: node is mostly stabilized by artificial leakage
}
```

## Bottom line

The underlying warning is directionally right: filters are not a real cure for unstable iterative physics.

But for this codebase the correct translation is:

- **do not switch the electrical solver to push**
- **do add adaptive `omega` and better diagnostics**
- **do keep tiny diagonal regularization only as conditioning**
- **do split acyclic logical/signal subgraphs from implicit physical solves where beneficial**

That is the most technically justified path with the current architecture.

## Adaptive `omega` implementation plan

Goal: improve stability of the current one-step-per-frame relaxed solve without changing the solver model.

### Design constraints

- keep current stamping model unchanged
- keep one SOR update per frame
- avoid global matrix assembly
- react only to observed instability/contraction

### Minimal implementation path

#### 1. Extend simulator state

Add fields to `/Users/vladimir/an24_cpp/src/jit_solver/simulator.h`:

```cpp
float omega_ = SOR::OMEGA;
float prev_convergence_error_ = 0.0f;
int worsening_streak_ = 0;
int improving_streak_ = 0;
bool adaptive_omega_enabled_ = true;
```

Optional constants:

```cpp
float omega_min_ = 1.0f;
float omega_max_ = 1.45f;
float omega_downscale_ = 0.90f;
float omega_upscale_ = 1.02f;
```

#### 2. Reuse existing convergence metric

The project already computes a useful error proxy:

- `save_convergence_state()` before update
- `get_max_change()` after update

That means no new residual buffer is required.

Relevant files:

- `/Users/vladimir/an24_cpp/src/jit_solver/simulator.cpp`
- `/Users/vladimir/an24_cpp/src/jit_solver/state.cpp`

#### 3. Update `omega` after each frame

In `/Users/vladimir/an24_cpp/src/jit_solver/simulator.cpp`, immediately after the SOR step, evaluate:

```cpp
float err = state_.get_max_change();

if (adaptive_omega_enabled_) {
    if (prev_convergence_error_ > 0.0f && err > prev_convergence_error_ * 1.05f) {
        worsening_streak_++;
        improving_streak_ = 0;
    } else if (prev_convergence_error_ > 0.0f && err < prev_convergence_error_ * 0.80f) {
        improving_streak_++;
        worsening_streak_ = 0;
    } else {
        worsening_streak_ = 0;
        improving_streak_ = 0;
    }

    if (worsening_streak_ >= 2) {
        omega_ = std::max(omega_min_, omega_ * omega_downscale_);
        worsening_streak_ = 0;
    }

    if (improving_streak_ >= 4) {
        omega_ = std::min(omega_max_, omega_ * omega_upscale_);
        improving_streak_ = 0;
    }
}

prev_convergence_error_ = err;
```

Why this shape:

- reduce quickly on instability
- increase slowly on stable contraction
- avoid thrashing from one noisy frame

#### 4. Reset adaptive state on start/stop

In `/Users/vladimir/an24_cpp/src/jit_solver/simulator.cpp` reset on `start_from_json()` and `stop()`:

```cpp
omega_ = SOR::OMEGA;
prev_convergence_error_ = 0.0f;
worsening_streak_ = 0;
improving_streak_ = 0;
```

#### 5. Add diagnostics

Expose for UI/tests:

```cpp
float get_omega() const { return omega_; }
```

Recommended logs/warnings:

- current `omega`
- current `max_change`
- warning when `omega_` is clamped to `omega_min_`

### Recommended defaults

Good first-pass values for this codebase:

```cpp
omega_min_ = 1.0f;
omega_max_ = 1.45f;
omega_downscale_ = 0.90f;
omega_upscale_ = 1.02f;
```

Rationale:

- `1.0` falls back to Gauss-Seidel-like behavior
- cap near current documented safe range in `/Users/vladimir/an24_cpp/src/jit_solver/SOR_constants.h`
- fast decrease, slow recovery is safer for nonlinear/stiff systems

### Validation plan

Add tests covering:

1. stable resistive circuit keeps `omega` near baseline
2. oscillatory/stiff case causes `omega` to decrease
3. after recovery, `omega` increases slowly but stays bounded
4. simulator reset restores `omega = SOR::OMEGA`

Likely test file:

- `/Users/vladimir/an24_cpp/tests/test_adaptive_omega.cpp`

### Non-goals

This plan does not:

- compute the full spectral radius
- replace SOR with push
- change physical stamping rules
- add strong diagonal damping/LM as a default

### Recommended rollout order

1. implement adaptive `omega` with logging only
2. run on known systems like `RU19A`, `GS24`, `RUG82`
3. inspect whether `omega` frequently collapses to `1.0`
4. only then tune thresholds or add per-domain heuristics

If adaptive `omega` materially improves stability without visible bias, it is the most cost-effective next solver upgrade for the current architecture.
