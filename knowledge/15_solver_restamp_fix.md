# SOR Solver Re-stamp Fix Plan

## Problem

The electrical SOR solver converges to **wrong values** on series circuits.

**Root cause**: Components stamp residuals once using stale `across[]` values, then the solver does N sweeps on those stale residuals. With `INNER_SWEEPS=1` this is effectively Jacobi iteration that relies on convergence across 60Hz simulation steps. For circuits with large conductance ratios in series (e.g., VariableConductance g=10S + Resistor g=0.2S), it converges to a **wrong fixed point** (26.9V instead of analytical 34.8V on the GSC excitation circuit, making the regulator stabilize at ~26.9V instead of the target 28.5V).

Increasing `INNER_SWEEPS` without re-stamping causes NaN divergence (residuals accumulate on stale stamps).

## Proof of Concept

Unit tests already use the correct pattern. See `tests/test_transformer.cpp` line 236:
```cpp
for (int iter = 0; iter < 100; ++iter) {
    st.clear_through();
    // ... stamp all components ...
    st.precompute_inv_conductance();
    solve_sor_iteration(st.across.data(), st.through.data(), st.inv_conductance.data(), ...);
}
```
This converges correctly because stamps are refreshed each iteration.

## The Fix

### What to change

Move `clear_through()`, component stamping, and `precompute_inv_conductance()` **inside** the inner sweep loop, so each sweep uses freshly-computed residuals from updated `across[]` values.

### Current code (broken)

Both SOR passes (Phase 2 and Phase 6) currently do:

```
clear_through()
stamp_all_components()
precompute_inv_conductance()
save_convergence_state()
for (INNER_SWEEPS) {
    solve_sor_iteration()   // stale stamps after first sweep
}
```

### Target code (fixed)

```
save_convergence_state()
for (INNER_SWEEPS) {
    clear_through()
    stamp_all_components()
    precompute_inv_conductance()
    solve_sor_iteration()   // always uses fresh stamps
}
```

Note: `save_convergence_state()` moves **before** the loop (it captures the state before any sweeps for convergence measurement / adaptive omega).

### INNER_SWEEPS value

With re-stamping, increasing `INNER_SWEEPS` is now safe and meaningful. Start with `INNER_SWEEPS = 4`. This gives 4 Gauss-Seidel-like iterations per simulation step, which should be enough for series circuits to converge within a step. Can tune later.

## Files to Modify

### 1. `src/jit_solver/simulator.cpp` (JIT runtime)

**Phase 2 (first SOR pass) — lines 158-181:**

Before:
```cpp
// == Phase 1: passive electrical stamp ==
state_.clear_through();
for (auto* variant : build_result_->phase_components.electrical_passive) {
    std::visit([&](auto& comp) {
        if constexpr (requires { comp.stamp_electrical_passive(state_, dt); }) {
            comp.stamp_electrical_passive(state_, dt);
        } else if constexpr (requires { comp.solve_electrical(state_, dt); }) {
            comp.solve_electrical(state_, dt);
        }
    }, *variant);
}

// == Phase 2: first SOR pass ==
state_.precompute_inv_conductance();
state_.save_convergence_state();
for (int iter = 0; iter < SOR::INNER_SWEEPS; ++iter) {
    solve_sor_iteration(
        state_.across.data(),
        state_.through.data(),
        state_.inv_conductance.data(),
        state_.dynamic_signals_count,
        omega_
    );
}
```

After:
```cpp
// == Phase 1+2: passive electrical stamp + SOR ==
state_.save_convergence_state();
for (int iter = 0; iter < SOR::INNER_SWEEPS; ++iter) {
    state_.clear_through();
    for (auto* variant : build_result_->phase_components.electrical_passive) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.stamp_electrical_passive(state_, dt); }) {
                comp.stamp_electrical_passive(state_, dt);
            } else if constexpr (requires { comp.solve_electrical(state_, dt); }) {
                comp.solve_electrical(state_, dt);
            }
        }, *variant);
    }
    state_.precompute_inv_conductance();
    solve_sor_iteration(
        state_.across.data(),
        state_.through.data(),
        state_.inv_conductance.data(),
        state_.dynamic_signals_count,
        omega_
    );
}
```

**Phase 6 (second SOR pass) — lines 210-238:**

Before:
```cpp
// == Phase 5: actuator electrical stamp + second SOR ==
state_.clear_through();
for (auto* variant : build_result_->phase_components.electrical_passive) {
    std::visit([&](auto& comp) {
        if constexpr (requires { comp.stamp_electrical_passive(state_, dt); }) {
            comp.stamp_electrical_passive(state_, dt);
        } else if constexpr (requires { comp.solve_electrical(state_, dt); }) {
            comp.solve_electrical(state_, dt);
        }
    }, *variant);
}
for (auto* variant : build_result_->phase_components.electrical_actuator) {
    std::visit([&](auto& comp) {
        if constexpr (requires { comp.stamp_electrical_actuator(state_, dt); }) {
            comp.stamp_electrical_actuator(state_, dt);
        }
    }, *variant);
}
state_.precompute_inv_conductance();
state_.save_convergence_state();
for (int iter = 0; iter < SOR::INNER_SWEEPS; ++iter) {
    solve_sor_iteration(
        state_.across.data(),
        state_.through.data(),
        state_.inv_conductance.data(),
        state_.dynamic_signals_count,
        omega_
    );
}
```

After:
```cpp
// == Phase 6: passive + actuator stamp + second SOR ==
state_.save_convergence_state();
for (int iter = 0; iter < SOR::INNER_SWEEPS; ++iter) {
    state_.clear_through();
    for (auto* variant : build_result_->phase_components.electrical_passive) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.stamp_electrical_passive(state_, dt); }) {
                comp.stamp_electrical_passive(state_, dt);
            } else if constexpr (requires { comp.solve_electrical(state_, dt); }) {
                comp.solve_electrical(state_, dt);
            }
        }, *variant);
    }
    for (auto* variant : build_result_->phase_components.electrical_actuator) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.stamp_electrical_actuator(state_, dt); }) {
                comp.stamp_electrical_actuator(state_, dt);
            }
        }, *variant);
    }
    state_.precompute_inv_conductance();
    solve_sor_iteration(
        state_.across.data(),
        state_.through.data(),
        state_.inv_conductance.data(),
        state_.dynamic_signals_count,
        omega_
    );
}
```

### 2. `src/codegen/codegen.cpp` (AOT codegen)

Apply the same structural change to the generated code. The codegen emits `step_N()` methods.

**Phase 2 (lines 509-520):**

Before:
```cpp
oss << "    // Phase 1: passive electrical stamp\n";
oss << "    st->clear_through();\n";
for (const auto& dev_name : phase_electrical_passive) {
    oss << "    " << sanitize_name(dev_name) << ".solve_electrical(*st, dt);\n";
}
oss << "    // Phase 2: first SOR pass\n";
oss << "    st->precompute_inv_conductance();\n";
oss << "    st->save_convergence_state();\n";
oss << "    for (int iter = 0; iter < SOR::INNER_SWEEPS; ++iter) {\n";
oss << "        solve_sor_iteration(st->across.data(), st->through.data(), st->inv_conductance.data(), st->dynamic_signals_count, SOR::OMEGA);\n";
oss << "    }\n";
```

After:
```cpp
oss << "    // Phase 1+2: passive electrical stamp + SOR (re-stamp each sweep)\n";
oss << "    st->save_convergence_state();\n";
oss << "    for (int iter = 0; iter < SOR::INNER_SWEEPS; ++iter) {\n";
oss << "        st->clear_through();\n";
for (const auto& dev_name : phase_electrical_passive) {
    oss << "        " << sanitize_name(dev_name) << ".solve_electrical(*st, dt);\n";
}
oss << "        st->precompute_inv_conductance();\n";
oss << "        solve_sor_iteration(st->across.data(), st->through.data(), st->inv_conductance.data(), st->dynamic_signals_count, SOR::OMEGA);\n";
oss << "    }\n";
```

**Phase 6 (lines 538-550):**

Before:
```cpp
oss << "    // Phase 6: second electrical pass (passive + actuators)\n";
oss << "    st->clear_through();\n";
for (const auto& dev_name : phase_electrical_passive) {
    oss << "    " << sanitize_name(dev_name) << ".solve_electrical(*st, dt);\n";
}
for (const auto& dev_name : phase_electrical_actuator) {
    oss << "    " << sanitize_name(dev_name) << ".stamp_electrical_actuator(*st, dt);\n";
}
oss << "    st->precompute_inv_conductance();\n";
oss << "    st->save_convergence_state();\n";
oss << "    for (int iter = 0; iter < SOR::INNER_SWEEPS; ++iter) {\n";
oss << "        solve_sor_iteration(st->across.data(), st->through.data(), st->inv_conductance.data(), st->dynamic_signals_count, SOR::OMEGA);\n";
oss << "    }\n";
```

After:
```cpp
oss << "    // Phase 6: passive + actuator stamp + second SOR (re-stamp each sweep)\n";
oss << "    st->save_convergence_state();\n";
oss << "    for (int iter = 0; iter < SOR::INNER_SWEEPS; ++iter) {\n";
oss << "        st->clear_through();\n";
for (const auto& dev_name : phase_electrical_passive) {
    oss << "        " << sanitize_name(dev_name) << ".solve_electrical(*st, dt);\n";
}
for (const auto& dev_name : phase_electrical_actuator) {
    oss << "        " << sanitize_name(dev_name) << ".stamp_electrical_actuator(*st, dt);\n";
}
oss << "        st->precompute_inv_conductance();\n";
oss << "        solve_sor_iteration(st->across.data(), st->through.data(), st->inv_conductance.data(), st->dynamic_signals_count, SOR::OMEGA);\n";
oss << "    }\n";
```

### 3. `src/jit_solver/SOR_constants.h`

Change `INNER_SWEEPS` from 1 to 4 and update the comment:

```cpp
/// Number of re-stamp + sweep iterations per SOR pass.
/// Each iteration: clear → stamp all components → precompute inv_g → sweep.
/// 4 is enough for series circuits with moderate conductance ratios.
constexpr int INNER_SWEEPS = 4;
```

## Verification

### 1. Build
```bash
cmake --build build -j$(nproc)
```

### 2. GSC convergence test (primary goal)
```bash
./build/examples/sim_debug GSC.blueprint --steps 300 --probe bus_2:v
```
Expected: bus voltage converges to ~28.5V (PI setpoint), not 26.9V.

### 3. Existing test suite
```bash
cd build && ctest --output-on-failure
```
All SOR convergence tests in `tests/` should still pass (they already use the correct re-stamp pattern manually, so they are independent of `INNER_SWEEPS`). The codegen tests (`test_codegen_accumulator`) will need attention — they pattern-match on emitted code, so the new Phase 1+2 comment/structure may break string expectations.

### 4. Performance sanity check
```bash
./build/examples/benchmark_jit_vs_aot
```
With `INNER_SWEEPS=4`, each SOR pass does 4x the stamping work. For typical blueprints (~20 electrical components), this adds ~80 stamp calls per step (from ~20). At 60Hz this is negligible. If concerned, profile and consider reducing to 2 or 3.

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Codegen test string matching breaks | Low | Update expected strings in `test_codegen_accumulator.cpp` |
| Performance regression from 4x stamps | Low | Profile; 4 sweeps of ~20 stamps is still < 1us on modern CPU |
| Adaptive omega interacts badly with re-stamp | Low | Adaptive omega reads `get_max_change()` which uses `save_convergence_state()` — moved before loop, still correct |
| Components with side effects in stamp | None | All stamp methods are idempotent (only read `across[]`, write to `through[]`/`conductance[]` which are cleared each iteration). `post_step()` handles persistent state. Verified across all 40 stamp methods. |
| `OMEGA > 1` with re-stamping causes oscillation | Low | If seen, reduce OMEGA to 1.0 (pure Gauss-Seidel). SOR_constants.h is the single knob. |

## Component Stamping Safety Confirmation

All 40 component stamp methods were audited. Every one:
- Only **reads** `st.across[]` (current voltages)
- Only **writes** to `st.through[]` and `st.conductance[]` (both cleared each iteration)
- Has **no side effects** on component internal state
- Is **idempotent** given the same `across[]` values

This means re-stamping between sweeps is safe — it will produce correct, updated residuals reflecting the latest voltage estimates.

## After This Fix

- Update `knowledge/sor_optimization.md` to document the re-stamp loop
- Update `knowledge/02_simulation.md` pipeline phase descriptions
- Regenerate `generated/generated_GSC.cpp` with updated codegen
- The 5 pre-existing test failures are unrelated to this fix and can be addressed separately
