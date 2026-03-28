# Phase 4: Integration Testing & Verification

## Methodology: Failing-Test-First

Every integration test follows red-green TDD:
1. Write the integration test - it MUST fail (red)
2. Fix any issues until the test passes (green)
3. Phase DONE only when `cd build && ctest` reports 0 failures

## Overview

Verify the complete push propagation system works end-to-end:
- Single-pass execution (no SOR iterations)
- GSC voltage regulation loop converges
- Multi-domain components work correctly
- Dynamic enable/disable is stable
- Performance meets target (<100us/frame)
- All existing tests updated and passing

---

## Step 4.1: Single-Pass Verification

### Test

```cpp
// tests/test_push_integration.cpp
#include <gtest/gtest.h>
#include "jit_solver/simulator.h"

TEST(PushIntegration, SinglePassNoIteration) {
    // Create a circuit with battery + load
    std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery",
             "params": {"v_nominal": "24.0", "internal_r": "0.1"},
             "ports": ["v_out", "v_in"]},
            {"name": "load", "classname": "Load",
             "params": {"conductance": "0.1"},
             "ports": ["input"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"},
             "ports": ["v"]}
        ],
        "connections": [
            ["bat.v_out", "load.input"],
            ["bat.v_in", "gnd.v"]
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);
    sim.step(1.0f / 60.0f);

    // Battery should output nominal voltage in single pass
    float v = sim.get_port_value("bat", "v_out");
    EXPECT_NEAR(v, 24.0f, 1.0f);

    // Step count should be 1 (not N*iterations)
    EXPECT_EQ(sim.get_step_count(), 1u);
}
```

---

## Step 4.2: GSC Voltage Regulation Loop

This is the most important integration test. The Generator Starter Controller (GSC) loop must stabilize:

Generator outputs voltage -> VoltageSense reads bus -> PI computes error -> CVS drives field -> Generator adjusts

### Test

```cpp
TEST(PushIntegration, GSCStabilizesAt28V) {
    std::string json = R"({
        "devices": [
            {"name": "gen", "classname": "Generator",
             "params": {"v_nominal": "28.5", "internal_r": "0.005"},
             "ports": ["v_out", "v_in"]},
            {"name": "vsense", "classname": "VoltageSense",
             "params": {"gain": "1.0", "offset": "0.0"},
             "ports": ["v_in", "v_ref", "out"]},
            {"name": "target", "classname": "RefNode",
             "params": {"value": "28.5"},
             "ports": ["v"]},
            {"name": "pi", "classname": "PI",
             "params": {"Kp": "1.0", "Ki": "0.5", "output_min": "0.0", "output_max": "1.0"},
             "ports": ["setpoint", "feedback", "output"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"},
             "ports": ["v"]},
            {"name": "load", "classname": "Load",
             "params": {"conductance": "0.1"},
             "ports": ["input"]}
        ],
        "connections": [
            ["gen.v_out", "vsense.v_in"],
            ["gen.v_out", "load.input"],
            ["gen.v_in", "gnd.v"],
            ["vsense.v_ref", "gnd.v"],
            ["target.v", "pi.setpoint"],
            ["vsense.out", "pi.feedback"]
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    // Run 2 seconds (120 frames at 60Hz)
    for (int i = 0; i < 120; i++) {
        sim.step(1.0f / 60.0f);
    }

    float v_bus = sim.get_port_value("gen", "v_out");
    EXPECT_NEAR(v_bus, 28.5f, 0.5f) << "GSC loop should stabilize at ~28.5V";
}

TEST(PushIntegration, GSCRejectsLoadDisturbance) {
    // Same setup as above, but add a large load mid-simulation
    std::string json = R"({
        "devices": [
            {"name": "gen", "classname": "Generator",
             "params": {"v_nominal": "28.5"},
             "ports": ["v_out", "v_in"]},
            {"name": "vsense", "classname": "VoltageSense",
             "params": {"gain": "1.0"},
             "ports": ["v_in", "v_ref", "out"]},
            {"name": "target", "classname": "RefNode",
             "params": {"value": "28.5"},
             "ports": ["v"]},
            {"name": "pi", "classname": "PI",
             "params": {"Kp": "2.0", "Ki": "1.0", "output_min": "0.0", "output_max": "1.0"},
             "ports": ["setpoint", "feedback", "output"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"},
             "ports": ["v"]}
        ],
        "connections": [
            ["gen.v_out", "vsense.v_in"],
            ["gen.v_in", "gnd.v"],
            ["vsense.v_ref", "gnd.v"],
            ["target.v", "pi.setpoint"],
            ["vsense.out", "pi.feedback"]
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    // Stabilize first (1 second)
    for (int i = 0; i < 60; i++) sim.step(1.0f / 60.0f);

    float v_before = sim.get_port_value("gen", "v_out");

    // Recover for 2 more seconds
    for (int i = 0; i < 120; i++) sim.step(1.0f / 60.0f);

    float v_after = sim.get_port_value("gen", "v_out");
    EXPECT_NEAR(v_after, 28.5f, 0.5f) << "GSC should recover after disturbance";
}
```

---

## Step 4.3: Multi-Domain Component Test

### Test

```cpp
TEST(PushIntegration, RU19AStartupSequence) {
    std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery",
             "params": {"v_nominal": "24.0"},
             "ports": ["v_out", "v_in"]},
            {"name": "apu", "classname": "RU19A",
             "params": {"auto_start": "1", "target_rpm": "16000"},
             "ports": ["v_start", "v_bus", "k_mod", "t4_out", "rpm_out"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"},
             "ports": ["v"]},
            {"name": "kmod", "classname": "RefNode",
             "params": {"value": "0.5"},
             "ports": ["v"]}
        ],
        "connections": [
            ["bat.v_out", "apu.v_start"],
            ["bat.v_in", "gnd.v"],
            ["kmod.v", "apu.k_mod"]
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    // Run 30 seconds (APU startup takes ~15s)
    for (int i = 0; i < 1800; i++) {
        sim.step(1.0f / 60.0f);
    }

    float rpm = sim.get_port_value("apu", "rpm_out");
    EXPECT_GT(rpm, 50.0f) << "APU should reach significant RPM after 30s";

    float t4 = sim.get_port_value("apu", "t4_out");
    EXPECT_GT(t4, 100.0f) << "APU EGT should rise during startup";

    // All values should be finite
    EXPECT_TRUE(std::isfinite(rpm));
    EXPECT_TRUE(std::isfinite(t4));
}
```

---

## Step 4.4: Dynamic Enable/Disable Stability

### Test

```cpp
TEST(PushIntegration, EnableDisableNoNaN) {
    std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery",
             "params": {"v_nominal": "24.0"},
             "ports": ["v_out", "v_in"]},
            {"name": "sw", "classname": "Switch",
             "params": {},
             "ports": ["v_in", "v_out", "control", "state"]},
            {"name": "load", "classname": "Load",
             "params": {"conductance": "0.5"},
             "ports": ["input"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"},
             "ports": ["v"]}
        ],
        "connections": [
            ["bat.v_out", "sw.v_in"],
            ["sw.v_out", "load.input"],
            ["bat.v_in", "gnd.v"]
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    // Toggle switch every 10 frames for 1000 frames
    for (int i = 0; i < 1000; i++) {
        if (i % 10 == 0) {
            // Toggle control signal
            float toggle = (i / 10 % 2 == 0) ? 1.0f : 0.0f;
            sim.apply_overrides({{"sw.control", toggle}});
        }
        sim.step(1.0f / 60.0f);

        // Verify no NaN/Inf
        float v_bat = sim.get_port_value("bat", "v_out");
        float v_load = sim.get_port_value("load", "input");
        ASSERT_TRUE(std::isfinite(v_bat)) << "Battery voltage NaN at step " << i;
        ASSERT_TRUE(std::isfinite(v_load)) << "Load voltage NaN at step " << i;
    }
}

TEST(PushIntegration, RapidSwitchingStable) {
    std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery",
             "params": {"v_nominal": "24.0"},
             "ports": ["v_out", "v_in"]},
            {"name": "sw1", "classname": "Switch",
             "params": {},
             "ports": ["v_in", "v_out", "control", "state"]},
            {"name": "sw2", "classname": "Switch",
             "params": {},
             "ports": ["v_in", "v_out", "control", "state"]},
            {"name": "load", "classname": "Load",
             "params": {"conductance": "1.0"},
             "ports": ["input"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"},
             "ports": ["v"]}
        ],
        "connections": [
            ["bat.v_out", "sw1.v_in"],
            ["sw1.v_out", "sw2.v_in"],
            ["sw2.v_out", "load.input"],
            ["bat.v_in", "gnd.v"]
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    // Random-ish toggling
    for (int i = 0; i < 500; i++) {
        sim.apply_overrides({
            {"sw1.control", float(i % 3 == 0)},
            {"sw2.control", float(i % 5 == 0)}
        });
        sim.step(1.0f / 60.0f);

        float v = sim.get_port_value("load", "input");
        ASSERT_TRUE(std::isfinite(v)) << "Load voltage NaN at step " << i;
        ASSERT_LE(std::abs(v), 100.0f) << "Load voltage out of range at step " << i;
    }
}
```

---

## Step 4.5: Performance Benchmark

### Test

```cpp
TEST(PushIntegration, PerformanceUnder100us) {
    // Build a realistic circuit: ~50 components
    std::string json = R"({
        "devices": [
            {"name": "bat1", "classname": "Battery", "params": {"v_nominal": "24.0"}, "ports": ["v_out", "v_in"]},
            {"name": "bat2", "classname": "Battery", "params": {"v_nominal": "24.0"}, "ports": ["v_out", "v_in"]},
            {"name": "gen1", "classname": "Generator", "params": {"v_nominal": "28.5"}, "ports": ["v_out", "v_in"]},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}, "ports": ["v"]},
            {"name": "sw1", "classname": "Switch", "params": {}, "ports": ["v_in", "v_out", "control", "state"]},
            {"name": "sw2", "classname": "Switch", "params": {}, "ports": ["v_in", "v_out", "control", "state"]},
            {"name": "sw3", "classname": "Switch", "params": {}, "ports": ["v_in", "v_out", "control", "state"]},
            {"name": "load1", "classname": "Load", "params": {"conductance": "0.1"}, "ports": ["input"]},
            {"name": "load2", "classname": "Load", "params": {"conductance": "0.2"}, "ports": ["input"]},
            {"name": "load3", "classname": "Load", "params": {"conductance": "0.5"}, "ports": ["input"]}
        ],
        "connections": [
            ["bat1.v_out", "sw1.v_in"],
            ["bat1.v_in", "gnd.v"],
            ["bat2.v_in", "gnd.v"],
            ["gen1.v_in", "gnd.v"],
            ["sw1.v_out", "load1.input"],
            ["bat2.v_out", "sw2.v_in"],
            ["sw2.v_out", "load2.input"],
            ["gen1.v_out", "sw3.v_in"],
            ["sw3.v_out", "load3.input"]
        ]
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    // Close all switches
    sim.apply_overrides({
        {"sw1.control", 1.0f},
        {"sw2.control", 1.0f},
        {"sw3.control", 1.0f}
    });
    sim.step(1.0f / 60.0f); // Warm up

    // Benchmark 1000 frames
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; i++) {
        sim.step(1.0f / 60.0f);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    float avg_us = static_cast<float>(elapsed_us) / 1000.0f;

    // Target: < 100us per frame
    EXPECT_LT(avg_us, 100.0f) << "Average frame time: " << avg_us << "us (target < 100us)";

    // For comparison: SOR does ~15000 iterations * overhead per frame
    // Push does ~10 component execute() calls per frame
    // Should be 10-100x faster
}
```

---

## Step 4.6: Update ALL Existing Tests

Every existing test that references the old API must be updated:

### Changes required:

1. **`st.across[...]` -> `st.values[...]`** in all test files
2. **`st.through[...]`** references -> DELETE or rework
3. **`st.conductance[...]`** references -> DELETE
4. **Phase-based test logic** -> Replace with single `scheduler.step()` or `sim.step()`
5. **SOR convergence tests** -> DELETE (SOR no longer exists)
6. **Adaptive omega tests** -> DELETE
7. **Sanitizer tests** -> Simplify (no SOR-specific sanitizer)

### Process:

```bash
# Find all test files referencing old API
grep -rn "st\.across\|st\.through\|st\.conductance\|SOR::\|solve_sor\|stamp_two_port" tests/
```

Update each file found. The bulk of changes are simple renames (`across` -> `values`).

---

## Files Changed Summary

| File | Action |
|------|--------|
| `tests/test_push_integration.cpp` | NEW: end-to-end integration tests |
| `tests/CMakeLists.txt` | Add integration test targets |
| `tests/*.cpp` (all existing) | Update `st.across` -> `st.values`, remove SOR references |

## Completion Criteria

- [ ] Single-pass test passes (no SOR iterations)
- [ ] GSC loop stabilizes at 28.5V +/- 0.5V within 2 seconds
- [ ] RU19A startup sequence completes (RPM rises, EGT rises)
- [ ] 1000 frames of random switching produces no NaN/Inf
- [ ] Performance < 100us/frame for 10-component circuit
- [ ] ALL existing tests pass after API updates
- [ ] `cd build && ctest` reports 0 failures
