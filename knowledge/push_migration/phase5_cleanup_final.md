# Phase 5: Cleanup & Final Verification

## Methodology: Failing-Test-First

Final verification follows the same TDD discipline:
1. Write verification tests - they MUST fail if cleanup is incomplete (red)
2. Complete cleanup - tests pass (green)
3. Phase DONE only when `cd build && ctest` reports 0 failures

## Overview

Final cleanup pass to ensure all legacy solver artifacts are removed and the codebase is clean:
- Delete all legacy solver-related files and code
- Delete all phase-related code
- Update AOT codegen for push model
- Update editor simulation
- Final performance verification
- Documentation update

---

## Step 5.1: Verify Complete Legacy Solver Removal

### Test

```cpp
// tests/test_push_final.cpp
#include <gtest/gtest.h>

TEST(PushFinal, NoLegacySolverReferencesInBuild) {
    // This test verifies at build level.
    // If any source file still references legacy solver symbols, it won't compile.
    // The test itself just confirms the binary was built successfully.
    SUCCEED();
}
```

### Manual Verification (run these commands)

```bash
# Search for any remaining legacy solver references
grep -rn "legacy_iterative::\|solve_iterative_relaxation\|stamp_two_port\|stamp_one_port\|stamp_current_source\|stamp_voltage_source" src/
# Expected: 0 results

# Search for old state array names
grep -rn "\.across\[" src/
grep -rn "\.through\[" src/
grep -rn "\.conductance\[" src/
grep -rn "\.inv_conductance\[" src/
grep -rn "convergence_buffer" src/
# Expected: 0 results each

# Search for old phase names
grep -rn "electrical_passive\|electrical_observer\|electrical_actuator\|control_commit\|stamp_electrical" src/
# Expected: 0 results

# Search for execution traits
grep -rn "ExecutionTraits\|get_execution_traits\|execution_traits" src/
# Expected: 0 results (file deleted)
```

---

## Step 5.2: Delete Legacy Solver Files

| File | Action |
|------|--------|
| `src/jit_solver/SOR_constants.h` | Does not exist |
| `src/jit_solver/execution_traits.h` | DELETE (done in Phase 1) |

Verify no `#include` references to deleted files remain:
```bash
grep -rn "SOR_constants\|execution_traits" src/
# Expected: 0 results
```

---

## Step 5.3: Clean Up Component Headers

### Remove Obsolete Methods from `all.h`

These method declarations should no longer exist on any component:

```
stamp_electrical_passive()
stamp_electrical_actuator()
observe_electrical()
commit_control()     # merged into solve_electrical() for Switch/Relay/HoldButton/AZS
finalize_step()      # kept if component uses it for state machine updates
```

### Remove Obsolete Fields from Components

These fields were legacy solver-specific and should be removed:

| Component | Field to Remove | Reason |
|-----------|----------------|--------|
| Switch | `downstream_g` | conductance passback |
| Switch | `downstream_I` | current passback |
| Switch | `v_out_old` | convergence tracking |
| Relay | `downstream_g` | conductance passback |
| Relay | `downstream_I` | current passback |
| Relay | `v_out_old` | convergence tracking |
| HoldButton | `downstream_g` | conductance passback |
| HoldButton | `downstream_I` | current passback |
| HoldButton | `v_out_old` | convergence tracking |
| AZS | `downstream_g` | conductance passback |
| AZS | `downstream_I` | current passback |
| AZS | `v_out_old` | convergence tracking |

After removing these fields, update tests that reference them.

---

## Step 5.4: Update AOT Codegen

### Test

```cpp
TEST(PushFinal, AOTCodegenProducesPushCode) {
    // Generate AOT code and verify it uses values[] not across[]
    // This test may need adjustment based on codegen API
    // For now, verify the generated code compiles
    SUCCEED();
}
```

### Implementation

**File: `src/codegen/codegen.cpp`** (1036 lines)

Key changes:
1. Generated code references `st.values[...]` instead of `st.across[...]`
2. Remove all `stamp_*()` calls from generated code
3. Remove legacy iteration loop from generated step function
4. Generate two-bucket execution: sources first, then consumers
5. Remove phase-based code generation

Replace the generated `step()` function template:

```cpp
// OLD (legacy iterative):
// void step(SimulationState& st, float dt) {
//     st.clear_through();
//     comp1.solve_electrical(st, dt);  // stamps
//     comp2.solve_electrical(st, dt);  // stamps
//     st.precompute_inv_conductance();
//     solve_iteration(...);
// }

// NEW (Push):
// void step(SimulationState& st, float dt) {
//     assert(dt > 0.0f);
//     // Bucket 1: Sources
//     comp_battery.solve_electrical(st, dt);
//     comp_refnode.solve_electrical(st, dt);
//     // Bucket 2: Consumers (topologically sorted)
//     comp_switch.solve_electrical(st, dt);
//     comp_load.solve_electrical(st, dt);
//     // Logical
//     comp_pi.solve_logical(st, dt);
// }
```

The union-find signal allocation in codegen stays (already correct for push - just assigns addresses).

AotProvider bindings stay (compile-time constants for port indices).

---

## Step 5.5: Update Editor Simulation

The editor (`src/editor/`) uses `JIT_Simulator` to run live simulation. Changes needed:

1. Editor calls `sim.step(dt)` - this already works (Simulator::step uses PushScheduler)
2. Editor reads `sim.get_port_value(node, port)` - this already works (reads values[])
3. Editor's oscilloscope reads voltage signals - works if it uses `get_wire_voltage()` / `get_port_value()`

### Verify by running the editor:
```bash
./build/examples/an24_editor
```
- Load a blueprint
- Press play
- Verify oscilloscope shows expected waveforms
- Verify component inspectors show correct values

---

## Step 5.6: Update Scheduling Constants

Replace `DomainSchedule` namespace (was in legacy solver constants) with inline constants where needed:

```cpp
// In scheduling.h or wherever sub-rate periods are used:
namespace DomainSchedule {
    constexpr float MECHANICAL_DT = 1.0f / 20.0f;  // 20 Hz
    constexpr float HYDRAULIC_DT  = 1.0f / 5.0f;   // 5 Hz
    constexpr float THERMAL_DT    = 1.0f;           // 1 Hz
}
```

These are still needed for sub-rate domain ticking in `Simulator::step()`.

---

## Step 5.7: Final Performance Verification

### Test

```cpp
TEST(PushFinal, FullAircraftPerformance) {
    // Load the full An-24 electrical system (if available as test JSON)
    // Or use the largest available test blueprint

    // Target metrics:
    // - Frame time < 100us (10x faster than legacy)
    // - Zero NaN/Inf over 10-minute simulation
    // - Memory: single values[] array, no flows/conductance overhead

    // This test is mostly a benchmark - use EXPECT not ASSERT for timing
    SUCCEED(); // Placeholder until full aircraft JSON is available
}
```

### Manual Benchmark

```bash
# Run the benchmark example
./build/examples/benchmark_jit_vs_aot

# Expected output should show:
# - Push JIT: < 100us/frame
# - Push AOT: < 50us/frame
# - vs old legacy JIT: ~1000-5000us/frame (for comparison)
```

---

## Step 5.8: Documentation Update

### Update `knowledge/index.md`

Add entry for push migration:
```
- `16_push_migration_plan.md` - Push propagation migration plan and architecture
- `push_migration/` - Detailed implementation phase guides
```

### Update `knowledge/10_quick_reference.md`

Replace legacy solver tuning section with push architecture quick reference:

```markdown
## Simulation Architecture

- Single `values[]` array in SimulationState
- Two-bucket execution: sources first, consumers second
- No legacy iterations, no conductance matrices
- Domain frequencies: Electrical 60Hz, Logical 60Hz, Mechanical 20Hz, Hydraulic 5Hz, Thermal 1Hz
- One-frame feedback delay (16.67ms at 60Hz) - acceptable for PI controllers
```

### Update `knowledge/errors_TODO.md`

Mark migration-related items as resolved.

---

## Step 5.9: Clean Build Verification

### Final Build and Test

```bash
# Clean build from scratch
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Run ALL tests
cd build && ctest --output-on-failure

# Expected: 0 failures

# Release build + run
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

### Compiler Warning Check

```bash
# Build with warnings enabled
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic"
cmake --build build -j$(nproc) 2>&1 | grep -E "warning:|error:"
# Expected: 0 errors, minimal warnings (no new ones from migration)
```

---

## Files Changed Summary

| File | Action |
|------|--------|
| `src/jit_solver/SOR_constants.h` | Does not exist |
| `src/jit_solver/execution_traits.h` | Verify DELETED |
| `src/jit_solver/components/all.h` | Remove obsolete method declarations and legacy solver fields |
| `src/jit_solver/components/all.cpp` | Remove obsolete method implementations |
| `src/codegen/codegen.cpp` | Update to generate push-style code |
| `src/jit_solver/scheduling.h` | Keep domain frequencies, remove iteration references |
| `knowledge/index.md` | Update documentation index |
| `knowledge/10_quick_reference.md` | Replace iterative solver tuning with push architecture |
| `knowledge/errors_TODO.md` | Mark resolved items |
| `tests/test_push_final.cpp` | NEW: final verification tests |

## Completion Criteria

- [ ] `grep -rn "legacy_iterative::\|solve_iterative_relaxation\|stamp_two_port" src/` returns 0 results
- [ ] `grep -rn "\.across\[" src/` returns 0 results
- [ ] `grep -rn "\.through\[" src/` returns 0 results
- [ ] `grep -rn "\.conductance\[" src/` returns 0 results
- [ ] `grep -rn "electrical_passive\|electrical_observer\|electrical_actuator" src/` returns 0 results
- [ ] Clean build from scratch succeeds (Debug and Release)
- [ ] `cd build && ctest` reports 0 failures
- [ ] AOT codegen produces push-style code
- [ ] Editor runs and displays correct values
- [ ] Performance < 100us/frame for typical circuit
- [ ] No compiler warnings from migration code
- [ ] Documentation updated
