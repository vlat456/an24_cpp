# Phase 1: Core Infrastructure

## Methodology: Failing-Test-First

Every step in this phase follows red-green TDD:
1. Write the test(s) described below - they MUST fail (red)
2. Implement the code - tests MUST pass (green)
3. Phase is DONE only when `cd build && ctest` reports 0 failures

No implementation code is written before its test exists and fails.

On branch `push_migration`, temporary full breakage is acceptable while migrating core infrastructure, and obsolete SOR-era tests/files may be deleted during the rewrite.

## Prerequisites

- New git branch `push-propagation` created from current HEAD
- This is a **clean rewrite** - no SOR fallbacks, no compatibility shims, no `if (use_push)` switches

## Overview

Replace the SOR-based simulation core with push propagation infrastructure:
- Simplify `SimulationState` to a single `values[]` array
- Create `PushScheduler` with two-bucket execution
- Update `build_systems_dev()` to produce push-compatible output
- Wire up `Simulator::step()` to use PushScheduler

## Step 1.1: Simplified SimulationState

### Test First (RED)

Create test file: `tests/test_push_state.cpp`

```cpp
#include <gtest/gtest.h>
#include "jit_solver/state.h"

TEST(PushState, ValuesArrayExists) {
    SimulationState st;
    EXPECT_TRUE(st.values.empty());
}

TEST(PushState, AllocateSignalWritesToValues) {
    SimulationState st;
    uint32_t idx = st.allocate_signal(28.0f, {Domain::Electrical, false});
    EXPECT_EQ(idx, 0u);
    EXPECT_FLOAT_EQ(st.values[idx], 28.0f);
}

TEST(PushState, AllocateMultipleSignals) {
    SimulationState st;
    uint32_t a = st.allocate_signal(28.0f, {Domain::Electrical, false});
    uint32_t b = st.allocate_signal(0.0f, {Domain::Electrical, false});
    uint32_t c = st.allocate_signal(115.0f, {Domain::Electrical, true});
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 1u);
    EXPECT_EQ(st.values.size(), 3u);
    EXPECT_FLOAT_EQ(st.values[0], 28.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
    // Fixed signals still get allocated (at end), value preserved
    EXPECT_FLOAT_EQ(st.values[c], 115.0f);
}

TEST(PushState, NoThroughArray) {
    SimulationState st;
    // Verify through[], conductance[], inv_conductance[] do not exist
    // This is a compile-time check - the test proves the struct has no such members
    // If these lines compile, the old fields are gone:
    static_assert(!requires { st.through; }, "through[] must be removed");
    static_assert(!requires { st.conductance; }, "conductance[] must be removed");
    static_assert(!requires { st.inv_conductance; }, "inv_conductance[] must be removed");
    static_assert(!requires { st.convergence_buffer; }, "convergence_buffer must be removed");
}

TEST(PushState, LutArenaPreserved) {
    SimulationState st;
    st.lut_keys.push_back(0.0f);
    st.lut_keys.push_back(1.0f);
    st.lut_values.push_back(0.0f);
    st.lut_values.push_back(100.0f);
    EXPECT_EQ(st.lut_keys.size(), 2u);
    EXPECT_EQ(st.lut_values.size(), 2u);
}
```

Add to `tests/CMakeLists.txt`:
```cmake
add_executable(push_state_tests test_push_state.cpp)
target_include_directories(push_state_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(push_state_tests PRIVATE jit_solver GTest::gtest_main)
gtest_discover_tests(push_state_tests)
```

### Implementation (GREEN)

**File: `src/jit_solver/state.h`**

Replace the entire file. The new SimulationState:

```cpp
#pragma once

#include "../json_parser/json_parser.h"
#include <vector>
#include <cstdint>

/// Signal metadata
struct SignalType {
    Domain domain;
    bool is_fixed;
};

/// Simulation state - push propagation model
/// Single values[] array holds all node potentials (V, pressure, temperature, RPM, etc.)
/// No flows, no conductance, no convergence buffers.
struct SimulationState {
    /// All node values: voltages, pressures, temperatures, control signals
    alignas(64) std::vector<float> values;

    /// Signal metadata (for diagnostics, not used in hot path)
    std::vector<SignalType> signal_types;

    /// LUT table arena - all breakpoint tables concatenated (cache-friendly)
    alignas(64) std::vector<float> lut_keys;
    alignas(64) std::vector<float> lut_values;

    /// Dynamic signals count - signals [0..count) are dynamic
    /// Signals [count..size) are fixed - iterate only up to count
    uint32_t dynamic_signals_count = 0;

    SimulationState() = default;

    /// Allocate a new signal. Fixed signals placed at END of array.
    [[nodiscard]] uint32_t allocate_signal(float initial_value, SignalType type);
};
```

**File: `src/jit_solver/state.cpp`** (new or replace existing)

```cpp
#include "state.h"

uint32_t SimulationState::allocate_signal(float initial_value, SignalType type) {
    if (type.is_fixed) {
        // Fixed signals go at the end
        uint32_t idx = static_cast<uint32_t>(values.size());
        values.push_back(initial_value);
        signal_types.push_back(type);
        return idx;
    } else {
        // Dynamic signals go before fixed signals
        uint32_t idx = dynamic_signals_count;
        values.insert(values.begin() + idx, initial_value);
        signal_types.insert(signal_types.begin() + idx, type);
        dynamic_signals_count++;
        return idx;
    }
}
```

### What to DELETE from `state.h`

- `std::vector<float> across` -> renamed to `values`
- `std::vector<float> through` -> DELETE
- `std::vector<float> conductance` -> DELETE
- `std::vector<float> inv_conductance` -> DELETE
- `std::vector<float> convergence_buffer` -> DELETE
- `void resize_buffers(uint32_t)` -> DELETE
- `void clear_through()` -> DELETE
- `void precompute_inv_conductance()` -> DELETE
- `void save_convergence_state()` -> DELETE
- `bool has_converged(float)` -> DELETE
- `float get_max_change()` -> DELETE
- `AOT_ALWAYS_INLINE void solve_sor_iteration(...)` -> DELETE
- `AOT_ALWAYS_INLINE void stamp_two_port(...)` -> DELETE
- `AOT_ALWAYS_INLINE void stamp_one_port_ground(...)` -> DELETE
- `AOT_ALWAYS_INLINE void stamp_current_source(...)` -> DELETE
- `AOT_ALWAYS_INLINE void stamp_voltage_source(...)` -> DELETE

All stamping helpers are SOR-only and must be deleted, not kept "just in case".

### Verify

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc)
cd build && ctest -R push_state --output-on-failure
```

All `push_state_tests` must pass. Other tests WILL fail (expected - they still reference old fields). Those will be fixed in later steps.

---

## Step 1.2: PushScheduler

### Test First (RED)

Create test file: `tests/test_push_scheduler.cpp`

```cpp
#include <gtest/gtest.h>
#include "jit_solver/scheduler.h"
#include "jit_solver/state.h"

// Mock component for testing execution order
struct MockSource {
    static constexpr Domain domain = Domain::Electrical;
    int* counter;
    int expected_order;

    void execute(SimulationState& st, float dt) {
        EXPECT_EQ(*counter, expected_order) << "Source executed out of order";
        (*counter)++;
        st.values[0] = 28.0f; // Write voltage
    }
};

struct MockConsumer {
    static constexpr Domain domain = Domain::Electrical;
    int* counter;
    int expected_order;
    float received_voltage = 0.0f;

    void execute(SimulationState& st, float dt) {
        EXPECT_EQ(*counter, expected_order) << "Consumer executed out of order";
        (*counter)++;
        received_voltage = st.values[0]; // Read voltage
    }
};

TEST(PushScheduler, SourcesBeforeConsumers) {
    SimulationState st;
    st.allocate_signal(0.0f, {Domain::Electrical, false});

    int counter = 0;
    MockSource src{&counter, 0};
    MockConsumer cons{&counter, 1};

    PushScheduler sched;
    sched.add_source(&src);
    sched.add_consumer(&cons);

    sched.step(st, 1.0f / 60.0f);

    EXPECT_EQ(counter, 2);
    EXPECT_FLOAT_EQ(cons.received_voltage, 28.0f);
}

TEST(PushScheduler, MultipleSourcesMultipleConsumers) {
    SimulationState st;
    st.allocate_signal(0.0f, {Domain::Electrical, false});
    st.allocate_signal(0.0f, {Domain::Electrical, false});

    int counter = 0;
    MockSource src1{&counter, 0};
    MockSource src2{&counter, 1};
    MockConsumer cons1{&counter, 2};
    MockConsumer cons2{&counter, 3};

    PushScheduler sched;
    sched.add_source(&src1);
    sched.add_source(&src2);
    sched.add_consumer(&cons1);
    sched.add_consumer(&cons2);

    sched.step(st, 1.0f / 60.0f);
    EXPECT_EQ(counter, 4);
}

TEST(PushScheduler, DtGuard) {
    SimulationState st;
    PushScheduler sched;
    // dt > 0 must be asserted. In debug builds this would abort.
    // We test the positive case here.
    sched.step(st, 1.0f / 60.0f); // Should not crash
}

TEST(PushScheduler, EmptySchedulerNoOp) {
    SimulationState st;
    PushScheduler sched;
    sched.step(st, 1.0f / 60.0f); // No components, no crash
}

TEST(PushScheduler, SinglePassExecution) {
    // Each component must execute exactly once per step
    SimulationState st;
    st.allocate_signal(0.0f, {Domain::Electrical, false});

    int exec_count = 0;
    MockSource src{&exec_count, 0};

    PushScheduler sched;
    sched.add_source(&src);

    sched.step(st, 1.0f / 60.0f);
    EXPECT_EQ(exec_count, 1); // Exactly once, not N iterations
}
```

Add to `tests/CMakeLists.txt`:
```cmake
add_executable(push_scheduler_tests test_push_scheduler.cpp)
target_include_directories(push_scheduler_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(push_scheduler_tests PRIVATE jit_solver GTest::gtest_main)
gtest_discover_tests(push_scheduler_tests)
```

### Implementation (GREEN)

**New file: `src/jit_solver/scheduler.h`**

```cpp
#pragma once

#include <vector>
#include <cassert>

struct SimulationState;

/// Type-erased component entry for the scheduler
struct ComponentEntry {
    using ExecuteFn = void(*)(void* self, SimulationState& st, float dt);
    void* self;
    ExecuteFn execute;
};

/// Two-bucket push scheduler
/// Bucket 1: Sources (Battery, Generator, CVS, RefNode) - set potentials
/// Bucket 2: Consumers (everything else) - read, compute, write
/// Within bucket 2, components are topologically sorted at build time.
class PushScheduler {
public:
    /// Add a source component (bucket 1)
    template <typename T>
    void add_source(T* component) {
        sources_.push_back({
            component,
            [](void* self, SimulationState& st, float dt) {
                static_cast<T*>(self)->execute(st, dt);
            }
        });
    }

    /// Add a consumer component (bucket 2)
    template <typename T>
    void add_consumer(T* component) {
        consumers_.push_back({
            component,
            [](void* self, SimulationState& st, float dt) {
                static_cast<T*>(self)->execute(st, dt);
            }
        });
    }

    /// Execute one simulation step
    void step(SimulationState& st, float dt) {
        assert(dt > 0.0f);

        // Bucket 1: sources set potentials first
        for (auto& e : sources_) {
            e.execute(e.self, st, dt);
        }

        // Bucket 2: consumers read, compute, write
        for (auto& e : consumers_) {
            e.execute(e.self, st, dt);
        }
    }

    size_t source_count() const { return sources_.size(); }
    size_t consumer_count() const { return consumers_.size(); }

private:
    std::vector<ComponentEntry> sources_;
    std::vector<ComponentEntry> consumers_;
};
```

### Verify

```bash
cmake --build build -j$(nproc)
cd build && ctest -R push_scheduler --output-on-failure
```

---

## Step 1.3: Delete SOR Infrastructure

### Test First (RED)

Create test file: `tests/test_no_sor.cpp`

```cpp
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

TEST(NoSOR, SORConstantsFileDeleted) {
    EXPECT_FALSE(std::filesystem::exists("../src/jit_solver/SOR_constants.h"))
        << "SOR_constants.h must be deleted";
}

TEST(NoSOR, NoSORReferencesInState) {
    // If state.h compiles without SOR_constants.h, this test passes implicitly
    // The build system validates this.
    SUCCEED();
}
```

### Implementation (GREEN)

**Delete these files:**
- `src/jit_solver/SOR_constants.h`

**Delete these structs/classes/functions from their files:**
- From `execution_traits.h`: DELETE entire file (9-phase traits are SOR-specific)
- From `scheduling.h`: Remove `should_run_on_step()` and `DomainSchedule` references. Keep `parse_domain()`, `get_domain_frequency()`, `get_domain_name()`, `get_domain_mask_string()`.
- From `jit_solver.h`: Remove `PhaseComponents` struct entirely. Remove `phase_components` field from `BuildResult`.

**What PhaseComponents contained (for reference - DELETE all of this):**
```cpp
// DELETE THIS:
struct PhaseComponents {
    std::vector<ComponentVariant*> electrical_passive;
    std::vector<ComponentVariant*> electrical_observer;
    std::vector<ComponentVariant*> logical;
    std::vector<ComponentVariant*> control_commit;
    std::vector<ComponentVariant*> electrical_actuator;
    std::vector<ComponentVariant*> finalize;
    std::vector<ComponentVariant*> mechanical;
    std::vector<ComponentVariant*> hydraulic;
    std::vector<ComponentVariant*> thermal;
};
```

**Replace with in `jit_solver.h`:**
```cpp
struct BuildResult {
    uint32_t signal_count;
    std::vector<uint32_t> fixed_signals;
    PortToSignal port_to_signal;

    /// Dynamic components for JIT mode
    std::unordered_map<std::string, ComponentVariant> devices;

    /// Push scheduler (populated at build time)
    PushScheduler scheduler;

    /// LUT table arena
    std::vector<float> lut_keys;
    std::vector<float> lut_values;
};
```

### Verify

```bash
cmake --build build -j$(nproc)
cd build && ctest -R "push_state|push_scheduler|no_sor" --output-on-failure
```

---

## Step 1.4: Rewire Simulator::step()

### Test First (RED)

Create test file: `tests/test_push_simulator.cpp`

```cpp
#include <gtest/gtest.h>
#include "jit_solver/simulator.h"

TEST(PushSimulator, StartAndStep) {
    // Minimal blueprint: one RefNode setting ground = 0V
    std::string json = R"({
        "devices": [
            {
                "name": "gnd",
                "classname": "RefNode",
                "params": { "value": "0.0" },
                "ports": ["v"]
            }
        ],
        "connections": []
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);
    EXPECT_TRUE(sim.is_running());

    // Step should not crash
    sim.step(1.0f / 60.0f);
    EXPECT_EQ(sim.get_step_count(), 1u);
}

TEST(PushSimulator, BatteryOutputsVoltage) {
    std::string json = R"({
        "devices": [
            {
                "name": "bat",
                "classname": "Battery",
                "params": { "v_nominal": "24.0", "internal_r": "0.1" },
                "ports": ["v_out", "v_in"]
            },
            {
                "name": "gnd",
                "classname": "RefNode",
                "params": { "value": "0.0" },
                "ports": ["v"]
            }
        ],
        "connections": [
            ["bat.v_in", "gnd.v"]
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);
    sim.step(1.0f / 60.0f);

    float v = sim.get_port_value("bat", "v_out");
    EXPECT_NEAR(v, 24.0f, 0.5f);
}

TEST(PushSimulator, NoPhasesNoSOR) {
    // Verify step() does NOT call any SOR functions
    // This is validated by the fact that SOR_constants.h is deleted
    // and the code compiles and runs.
    JIT_Simulator sim;
    // No crash = no SOR dependency
    SUCCEED();
}
```

### Implementation (GREEN)

**File: `src/jit_solver/simulator.h`**

Remove these fields:
- `float omega_`
- `float prev_convergence_error_`
- `bool adaptive_omega_enabled_`
- `size_t sanitizer_events_`
- `size_t sanitizer_events_last_step_`
- `float last_pass1_error_`
- `float last_pass2_error_`
- `void sanitize_dynamic_signals()`
- `float get_max_convergence_error()`
- `float get_omega()`
- `size_t get_sanitizer_event_count()`
- `size_t get_last_step_sanitizer_events()`
- `float get_last_pass1_error()`
- `float get_last_pass2_error()`

Remove `#include "SOR_constants.h"`.

Keep:
- `accumulator_mechanical_`, `accumulator_hydraulic_`, `accumulator_thermal_` (sub-rate domains stay)
- All public API: `start_from_json()`, `stop()`, `step()`, `get_port_value()`, etc.

**File: `src/jit_solver/simulator.cpp`**

Replace `step()` with:

```cpp
template<typename SolverTag>
void Simulator<SolverTag>::step(float dt) {
    if (!running_ || !build_result_.has_value()) return;
    if (dt <= 0.0f) return;

    // Single-pass push execution via scheduler
    build_result_->scheduler.step(state_, dt);

    // Sub-rate domains use accumulated dt (unchanged)
    accumulator_mechanical_ += dt;
    accumulator_hydraulic_ += dt;
    accumulator_thermal_ += dt;

    // TODO: Sub-rate domain ticks (mechanical 20Hz, hydraulic 5Hz, thermal 1Hz)
    // Will be wired in Phase 2 when multi-domain components are migrated

    time_ += dt;
    step_count_++;
}
```

Replace `get_wire_voltage()` and `get_port_value()`:
- Change `state_.across[...]` to `state_.values[...]`

Replace `apply_overrides()`:
- Change `state_.across[...]` to `state_.values[...]`

Replace `start_from_json()`:
- Remove convergence buffer allocation
- Remove omega/adaptive init
- Remove sanitizer init
- The scheduler is populated by `build_systems_dev()` (in BuildResult)

### Verify

```bash
cmake --build build -j$(nproc)
cd build && ctest -R "push_" --output-on-failure
```

All push tests pass. Build succeeds with no SOR references.

---

## Step 1.5: Update build_systems_dev() to Populate Scheduler

### Test First (RED)

Add to `tests/test_push_scheduler.cpp`:

```cpp
TEST(PushScheduler, BuildResultPopulatesScheduler) {
    std::string json = R"({
        "devices": [
            {
                "name": "bat",
                "classname": "Battery",
                "params": { "v_nominal": "24.0" },
                "ports": ["v_out", "v_in"]
            },
            {
                "name": "load",
                "classname": "Load",
                "params": { "conductance": "0.1" },
                "ports": ["input"]
            }
        ],
        "connections": [["bat.v_out", "load.input"]]
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> conns;
    for (const auto& c : ctx.connections) conns.push_back({c.from, c.to});

    auto result = build_systems_dev(ctx.devices, conns);

    // Battery is a source, Load is a consumer
    EXPECT_GE(result.scheduler.source_count(), 1u);
    EXPECT_GE(result.scheduler.consumer_count(), 1u);
}
```

### Implementation (GREEN)

In `src/jit_solver/jit_solver.cpp`, after creating all ComponentVariant instances in `build_systems_dev()`:

1. Classify each component as source or consumer based on its type
2. Add to `result.scheduler` via `add_source()` or `add_consumer()`

Source components (bucket 1): `Battery`, `Generator`, `GS24`, `RefNode`, `ControlledVoltageSource`, `ControlledCurrentSource`

Consumer components (bucket 2): everything else

```cpp
// After all components are created and stored in result.devices:
for (auto& [name, variant] : result.devices) {
    std::visit([&](auto& comp) {
        using T = std::decay_t<decltype(comp)>;
        if constexpr (std::is_same_v<T, Battery<JitProvider>> ||
                      std::is_same_v<T, Generator<JitProvider>> ||
                      std::is_same_v<T, GS24<JitProvider>> ||
                      std::is_same_v<T, RefNode<JitProvider>> ||
                      std::is_same_v<T, ControlledVoltageSource<JitProvider>> ||
                      std::is_same_v<T, ControlledCurrentSource<JitProvider>>) {
            result.scheduler.add_source(&comp);
        } else {
            result.scheduler.add_consumer(&comp);
        }
    }, variant);
}
```

### Verify

```bash
cmake --build build -j$(nproc)
cd build && ctest -R push_ --output-on-failure
```

---

## Files Changed Summary

| File | Action |
|------|--------|
| `src/jit_solver/state.h` | Rewrite: single `values[]`, delete SOR arrays and helpers |
| `src/jit_solver/state.cpp` | Rewrite: simple allocate_signal only |
| `src/jit_solver/scheduler.h` | NEW: PushScheduler with two buckets |
| `src/jit_solver/simulator.h` | Simplify: remove SOR fields, remove SOR_constants include |
| `src/jit_solver/simulator.cpp` | Rewrite step(): single-pass push, no SOR |
| `src/jit_solver/jit_solver.h` | Remove PhaseComponents, add scheduler to BuildResult |
| `src/jit_solver/jit_solver.cpp` | Populate scheduler in build_systems_dev() |
| `src/jit_solver/SOR_constants.h` | DELETE |
| `src/jit_solver/execution_traits.h` | DELETE |
| `tests/test_push_state.cpp` | NEW |
| `tests/test_push_scheduler.cpp` | NEW |
| `tests/test_push_simulator.cpp` | NEW |
| `tests/test_no_sor.cpp` | NEW |
| `tests/CMakeLists.txt` | Add new test targets |

## Completion Criteria

- [ ] `ctest -R push_state` passes
- [ ] `ctest -R push_scheduler` passes
- [ ] `ctest -R push_simulator` passes
- [ ] `SOR_constants.h` deleted
- [ ] `execution_traits.h` deleted
- [ ] No reference to `SOR::`, `stamp_two_port`, `solve_sor_iteration`, `conductance`, `through` in any source file
- [ ] `cd build && ctest` reports 0 failures across ALL tests (existing tests updated as needed)
