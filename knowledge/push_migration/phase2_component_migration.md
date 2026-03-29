# Phase 2: Component Migration

## Methodology: Failing-Test-First

Every component migration follows red-green TDD:
1. Write a test for the component's `execute()` method - it MUST fail (red)
2. Implement `execute()` - the test MUST pass (green)
3. Phase is DONE only when `cd build && ctest` reports 0 failures

## Overview

Migrate all 65+ components from legacy stamp-based methods to self-contained `execute()` methods.

**Key rules:**
- Keep existing method names (`solve_electrical`, `solve_mechanical`, etc.) but change internals
- Components read from `st.values[]`, compute physics, write to `st.values[]`
- No `stamp_two_port()`, no `stamp_one_port_ground()`, no conductance accumulation
- Precompute `inv_r = 1.0f / r` in `pre_load()`, use multiplication instead of division at runtime
- Branchless enable/disable: `output = computed * float(enabled)`
- No exceptions, no RTTI in hot path

## Port Access Pattern (Unchanged)

The Provider pattern stays exactly the same:
```cpp
float v = st.values[provider.get(PortNames::v_out)];  // read
st.values[provider.get(PortNames::v_out)] = 28.0f;    // write
```

Only difference: `st.across[...]` becomes `st.values[...]`.

---

## Step 2.1: Source Components (Bucket 1)

Sources SET values. They are authoritative writers.

### 2.1.1 RefNode

#### Test (RED)

Add to `tests/test_push_components.cpp`:
```cpp
#include <gtest/gtest.h>
#include "jit_solver/state.h"
#include "jit_solver/components/all.h"

TEST(PushComponents, RefNodeSetsValue) {
    SimulationState st;
    uint32_t v_idx = st.allocate_signal(999.0f, {Domain::Electrical, true});

    RefNode<JitProvider> ref;
    ref.provider.set(PortNames::v, v_idx);
    ref.value = 0.0f;

    ref.solve_electrical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[v_idx], 0.0f);
}
```

#### Implementation (GREEN)

```cpp
template <typename Provider>
void RefNode<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    st.values[provider.get(PortNames::v)] = value;
}
```

### 2.1.2 Battery

#### Test (RED)

```cpp
TEST(PushComponents, BatteryOutputsNominalVoltage) {
    SimulationState st;
    uint32_t v_out = st.allocate_signal(0.0f, {Domain::Electrical, false});
    uint32_t v_in = st.allocate_signal(0.0f, {Domain::Electrical, false});

    Battery<JitProvider> bat;
    bat.provider.set(PortNames::v_out, v_out);
    bat.provider.set(PortNames::v_in, v_in);
    bat.v_nominal = 24.0f;
    bat.internal_r = 0.1f;
    bat.pre_load();

    bat.solve_electrical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[v_out], 24.0f, 0.5f);
}

TEST(PushComponents, BatteryChargingFromGenerator) {
    SimulationState st;
    uint32_t v_out = st.allocate_signal(24.0f, {Domain::Electrical, false});
    uint32_t v_in = st.allocate_signal(0.0f, {Domain::Electrical, false});
    // If battery has a dc_in pin for generator feedback:
    uint32_t dc_in = st.allocate_signal(28.5f, {Domain::Electrical, false});

    Battery<JitProvider> bat;
    bat.provider.set(PortNames::v_out, v_out);
    bat.provider.set(PortNames::v_in, v_in);
    bat.v_nominal = 24.0f;
    bat.charge = 800.0f;
    bat.capacity = 1000.0f;
    bat.internal_r = 0.1f;
    bat.pre_load();

    // Run 60 frames (1 second) - battery should still output voltage
    for (int i = 0; i < 60; i++) {
        bat.solve_electrical(st, 1.0f / 60.0f);
    }
    EXPECT_GT(st.values[v_out], 20.0f);
}
```

#### Implementation (GREEN)

```cpp
template <typename Provider>
void Battery<Provider>::solve_electrical(SimulationState& st, float dt) {
    // Battery is a source: writes its terminal voltage based on SOC
    float soc = charge / std::max(capacity, 1.0f);
    st.values[provider.get(PortNames::v_out)] = v_nominal * soc;
    // v_in (ground reference) is set by RefNode, not by battery
}
```

### 2.1.3 Generator

#### Test (RED)

```cpp
TEST(PushComponents, GeneratorOutputsVoltage) {
    SimulationState st;
    uint32_t v_out = st.allocate_signal(0.0f, {Domain::Electrical, false});
    uint32_t v_in = st.allocate_signal(0.0f, {Domain::Electrical, false});

    Generator<JitProvider> gen;
    gen.provider.set(PortNames::v_out, v_out);
    gen.provider.set(PortNames::v_in, v_in);
    gen.v_nominal = 28.5f;
    gen.pre_load();

    gen.solve_electrical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[v_out], 28.5f, 0.5f);
}
```

#### Implementation (GREEN)

```cpp
template <typename Provider>
void Generator<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Generator outputs nominal voltage (RPM check done elsewhere)
    st.values[provider.get(PortNames::v_out)] = v_nominal;
}
```

### 2.1.4 ControlledVoltageSource (CVS)

#### Test (RED)

```cpp
TEST(PushComponents, CVSOutputsCommandedVoltage) {
    SimulationState st;
    uint32_t cmd = st.allocate_signal(0.5f, {Domain::Logical, false});
    uint32_t v_pos = st.allocate_signal(0.0f, {Domain::Electrical, false});
    uint32_t v_neg = st.allocate_signal(0.0f, {Domain::Electrical, false});

    ControlledVoltageSource<JitProvider> cvs;
    cvs.provider.set(PortNames::cmd, cmd);
    cvs.provider.set(PortNames::v_pos, v_pos);
    cvs.provider.set(PortNames::v_neg, v_neg);
    cvs.gain = 56.0f;  // 0.5 * 56 = 28V
    cvs.offset = 0.0f;
    cvs.min_v = 0.0f;
    cvs.max_v = 30.0f;

    cvs.solve_electrical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[v_pos], 28.0f, 0.1f);
}
```

#### Implementation (GREEN)

```cpp
template <typename Provider>
void ControlledVoltageSource<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float cmd_val = st.values[provider.get(PortNames::cmd)];
    float v_source = std::clamp(cmd_val * gain + offset, min_v, max_v);
    st.values[provider.get(PortNames::v_pos)] = v_source;
    // v_neg is reference (set by RefNode or another source)
}
```

### 2.1.5 GS24 (Starter-Generator)

#### Test (RED)

```cpp
TEST(PushComponents, GS24GeneratorMode) {
    SimulationState st;
    uint32_t v_out = st.allocate_signal(0.0f, {Domain::Electrical, false});

    GS24<JitProvider> gs;
    gs.provider.set(PortNames::v_out, v_out);
    gs.mode = GS24Mode::GENERATOR;
    gs.current_rpm = 15000.0f;
    gs.target_rpm = 15000.0f;
    gs.v_nominal = 28.5f;
    gs.pre_load();

    gs.solve_electrical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[v_out], 28.5f, 1.0f);
}

TEST(PushComponents, GS24StarterMode) {
    SimulationState st;
    uint32_t v_out = st.allocate_signal(24.0f, {Domain::Electrical, false});

    GS24<JitProvider> gs;
    gs.provider.set(PortNames::v_out, v_out);
    gs.mode = GS24Mode::STARTER;
    gs.current_rpm = 0.0f;
    gs.target_rpm = 15000.0f;
    gs.pre_load();

    gs.solve_electrical(st, 1.0f / 60.0f);
    // In starter mode, GS24 draws current (load), doesn't generate
    // v_out should not increase
    EXPECT_LE(st.values[v_out], 24.0f);
}
```

#### Implementation (GREEN)

```cpp
template <typename Provider>
void GS24<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float rpm_pct = current_rpm * inv_target_rpm;

    // Generator mode: output voltage proportional to RPM and k_mod
    float gen_mask = (mode == GS24Mode::GENERATOR) ? 1.0f : 0.0f;
    float phi = std::clamp((rpm_pct - rpm_threshold) * 5.0f, 0.0f, 1.0f);
    float k_mod_val = provider.has(PortNames::k_mod)
        ? st.values[provider.get(PortNames::k_mod)] : 1.0f;
    float v_gen = v_nominal * phi * k_mod_val;

    // Starter mode: acts as load (don't override bus voltage)
    float starter_mask = (mode == GS24Mode::STARTER) ? 1.0f : 0.0f;
    float v_current = st.values[provider.get(PortNames::v_out)];

    // Combine: generator sets voltage, starter passes through existing
    st.values[provider.get(PortNames::v_out)] =
        v_gen * gen_mask + v_current * starter_mask;
}
```

---

## Step 2.2: Switch-Type Components (Pass-Through)

Switches read input, conditionally pass to output. Pattern:
```cpp
output = input * float(closed);  // branchless
```

### 2.2.1 Switch

#### Test (RED)

```cpp
TEST(PushComponents, SwitchPassesVoltageWhenClosed) {
    SimulationState st;
    uint32_t v_in = st.allocate_signal(28.0f, {Domain::Electrical, false});
    uint32_t v_out = st.allocate_signal(0.0f, {Domain::Electrical, false});
    uint32_t control = st.allocate_signal(0.0f, {Domain::Electrical, false});
    uint32_t state_port = st.allocate_signal(0.0f, {Domain::Electrical, false});

    Switch<JitProvider> sw;
    sw.provider.set(PortNames::v_in, v_in);
    sw.provider.set(PortNames::v_out, v_out);
    sw.provider.set(PortNames::control, control);
    sw.provider.set(PortNames::state, state_port);
    sw.closed = true;

    sw.solve_electrical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[v_out], 28.0f);
    EXPECT_FLOAT_EQ(st.values[state_port], 1.0f);
}

TEST(PushComponents, SwitchBlocksVoltageWhenOpen) {
    SimulationState st;
    uint32_t v_in = st.allocate_signal(28.0f, {Domain::Electrical, false});
    uint32_t v_out = st.allocate_signal(0.0f, {Domain::Electrical, false});
    uint32_t control = st.allocate_signal(0.0f, {Domain::Electrical, false});
    uint32_t state_port = st.allocate_signal(0.0f, {Domain::Electrical, false});

    Switch<JitProvider> sw;
    sw.provider.set(PortNames::v_in, v_in);
    sw.provider.set(PortNames::v_out, v_out);
    sw.provider.set(PortNames::control, control);
    sw.provider.set(PortNames::state, state_port);
    sw.closed = false;

    sw.solve_electrical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[v_out], 0.0f);
    EXPECT_FLOAT_EQ(st.values[state_port], 0.0f);
}
```

#### Implementation (GREEN)

```cpp
template <typename Provider>
void Switch<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v = st.values[provider.get(PortNames::v_in)];
    st.values[provider.get(PortNames::v_out)] = v * float(closed);
    st.values[provider.get(PortNames::state)] = float(closed);
}

template <typename Provider>
void Switch<Provider>::commit_control(SimulationState& st, float /*dt*/) {
    float current_control = st.values[provider.get(PortNames::control)];
    if (std::abs(current_control - last_control) > 0.1f) {
        closed = !closed;
    }
    last_control = current_control;
}
```

### 2.2.2 Relay

Same pattern as Switch but with threshold:

```cpp
template <typename Provider>
void Relay<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v = st.values[provider.get(PortNames::v_in)];
    st.values[provider.get(PortNames::v_out)] = v * float(closed);
}

template <typename Provider>
void Relay<Provider>::commit_control(SimulationState& st, float /*dt*/) {
    float control = st.values[provider.get(PortNames::control)];
    closed = control > hold_threshold;
}
```

### 2.2.3 HoldButton, AZS

Follow the same pass-through pattern. AZS adds thermal trip logic in `solve_thermal()`.

---

## Step 2.3: Consumer Components

### 2.3.1 Load

#### Test (RED)

```cpp
TEST(PushComponents, LoadDoesNotModifyInput) {
    SimulationState st;
    uint32_t input = st.allocate_signal(28.0f, {Domain::Electrical, false});

    Load<JitProvider> load;
    load.provider.set(PortNames::input, input);
    load.conductance = 0.1f;

    load.solve_electrical(st, 1.0f / 60.0f);
    // Load reads voltage, computes power internally. Does NOT modify the bus.
    // (Current is computed locally, not written to flows[])
    EXPECT_FLOAT_EQ(st.values[input], 28.0f);
}
```

#### Implementation (GREEN)

```cpp
template <typename Provider>
void Load<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Load reads bus voltage. In push model, loads don't modify source voltage.
    // Current is implicit: I = V * G (computed locally if needed, not written to shared state)
    // No-op for values[] - the load just exists and draws power.
}
```

### 2.3.2 Resistor

In push model, standalone Resistor between two ports is a voltage divider:

```cpp
template <typename Provider>
void Resistor<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Resistor passes voltage from higher to lower side with drop
    // In push model: downstream side gets upstream voltage minus I*R drop
    // But since we don't track current globally, resistor is effectively a pass-through
    // with the drop being modeled by the component that cares about it.
    // Most standalone Resistors will be absorbed into upstream/downstream components.
}
```

### 2.3.3 CurrentSense

#### Test (RED)

```cpp
TEST(PushComponents, CurrentSenseComputesCurrent) {
    SimulationState st;
    uint32_t v_in = st.allocate_signal(28.0f, {Domain::Electrical, false});
    uint32_t v_out = st.allocate_signal(27.9f, {Domain::Electrical, false});
    uint32_t i_out = st.allocate_signal(0.0f, {Domain::Electrical, false});

    CurrentSense<JitProvider> cs;
    cs.provider.set(PortNames::v_in, v_in);
    cs.provider.set(PortNames::v_out, v_out);
    cs.provider.set(PortNames::i_out, i_out);
    cs.conductance = 1000.0f; // 1 milliohm sense resistor

    cs.solve_electrical(st, 1.0f / 60.0f);
    // I = (V_in - V_out) * G = 0.1 * 1000 = 100A
    EXPECT_NEAR(st.values[i_out], 100.0f, 1.0f);
}
```

#### Implementation (GREEN)

```cpp
template <typename Provider>
void CurrentSense<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v_diff = st.values[provider.get(PortNames::v_in)]
                 - st.values[provider.get(PortNames::v_out)];
    st.values[provider.get(PortNames::i_out)] = v_diff * conductance;
}

// observe_electrical is no longer needed - DELETE
```

### 2.3.4 Voltmeter

```cpp
template <typename Provider>
void Voltmeter<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Pure observer - reads voltage for display, no state modification needed
    // The bus voltage is already in values[v_in] for UI to read
}
```

### 2.3.5 IndicatorLight

```cpp
template <typename Provider>
void IndicatorLight<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v_diff = st.values[provider.get(PortNames::v_in)]
                 - st.values[provider.get(PortNames::v_out)];
    float normalized = std::clamp(v_diff * inv_rated_voltage, 0.0f, 1.0f);
    st.values[provider.get(PortNames::brightness)] = normalized * max_brightness;
}
```

---

## Step 2.4: Logical Components

These already read/write `st.across[]` directly - only rename to `st.values[]`.

### All Logical Components (Bulk Rename)

The following components ONLY need `st.across` -> `st.values` rename:

- `PID`, `PI`, `PD`, `P` - controllers
- `Add`, `Subtract`, `Multiply`, `Divide` - math
- `AND`, `OR`, `XOR`, `NOT`, `NAND` - logic gates
- `Comparator` - comparator with hysteresis
- `Any_V_to_Bool`, `Positive_V_to_Bool` - converters
- `LUT` - lookup table
- `FastTMO`, `AsymTMO` - time filters
- `SlewRate`, `AsymSlewRate` - rate limiters
- `TimeDelay`, `Monostable` - timing
- `SampleHold`, `Integrator` - state
- `Clamp`, `Normalize` - range
- `Min`, `Max`, `Greater`, `Lesser`, `GreaterEq`, `LesserEq` - comparison
- `Slider` - UI control

#### Test (RED) - Representative

```cpp
TEST(PushComponents, PIControllerConverges) {
    SimulationState st;
    uint32_t sp = st.allocate_signal(28.5f, {Domain::Logical, false});
    uint32_t fb = st.allocate_signal(24.0f, {Domain::Logical, false});
    uint32_t out = st.allocate_signal(0.0f, {Domain::Logical, false});

    PI<JitProvider> pi;
    pi.provider.set(PortNames::setpoint, sp);
    pi.provider.set(PortNames::feedback, fb);
    pi.provider.set(PortNames::output, out);
    pi.Kp = 1.0f;
    pi.Ki = 0.5f;
    pi.output_min = 0.0f;
    pi.output_max = 1.0f;

    pi.solve_logical(st, 1.0f / 60.0f);
    EXPECT_GT(st.values[out], 0.0f); // Error > 0, output should be positive
}
```

#### Implementation (GREEN)

For ALL logical components: global find-and-replace `st.across[` -> `st.values[` in `all.cpp`.

Also remove `solve_electrical()` from PID/PI/PD/P - they no longer need to stamp conductance into MNA matrix:

```cpp
// DELETE these methods entirely:
template <typename Provider>
void PID<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    st.conductance[provider.get(PortNames::output)] += 1e-6f;  // DELETE
}
```

The logical components only need `solve_logical()`.

---

## Step 2.5: Domain Bridge Components

### VoltageSense

#### Test (RED)

```cpp
TEST(PushComponents, VoltageSenseReadsVoltage) {
    SimulationState st;
    uint32_t v_in = st.allocate_signal(28.0f, {Domain::Electrical, false});
    uint32_t v_ref = st.allocate_signal(0.0f, {Domain::Electrical, false});
    uint32_t out = st.allocate_signal(0.0f, {Domain::Logical, false});

    VoltageSense<JitProvider> vs;
    vs.provider.set(PortNames::v_in, v_in);
    vs.provider.set(PortNames::v_ref, v_ref);
    vs.provider.set(PortNames::out, out);
    vs.gain = 1.0f;
    vs.offset = 0.0f;

    vs.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[out], 28.0f);
}
```

#### Implementation (GREEN)

```cpp
template <typename Provider>
void VoltageSense<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    float v = st.values[provider.get(PortNames::v_in)];
    float vref = st.values[provider.get(PortNames::v_ref)];
    st.values[provider.get(PortNames::out)] = (v - vref) * gain + offset;
}

// DELETE: solve_electrical() - no longer needed (no MNA stamping)
// DELETE: observe_electrical() - no longer needed (no legacy phases)
```

---

## Step 2.6: Multi-Domain Components

### ElectricPump

#### Test (RED)

```cpp
TEST(PushComponents, ElectricPumpBuildsPressure) {
    SimulationState st;
    uint32_t v_in = st.allocate_signal(28.0f, {Domain::Electrical, false});
    uint32_t p_in = st.allocate_signal(0.0f, {Domain::Hydraulic, false});
    uint32_t p_out = st.allocate_signal(0.0f, {Domain::Hydraulic, false});

    ElectricPump<JitProvider> pump;
    pump.provider.set(PortNames::v_in, v_in);
    pump.provider.set(PortNames::p_in, p_in);
    pump.provider.set(PortNames::p_out, p_out);
    pump.max_pressure = 1000.0f;

    pump.solve_hydraulic(st, 1.0f / 5.0f);
    EXPECT_GT(st.values[p_out], 0.0f);
}
```

#### Implementation (GREEN)

```cpp
template <typename Provider>
void ElectricPump<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // In push model: pump is a load on the electrical bus
    // Current draw is computed locally, does not modify bus voltage
}

template <typename Provider>
void ElectricPump<Provider>::solve_hydraulic(SimulationState& st, float /*dt*/) {
    float v = st.values[provider.get(PortNames::v_in)];
    float p_in_h = st.values[provider.get(PortNames::p_in)];
    float target_p = v * max_pressure / 28.0f;
    st.values[provider.get(PortNames::p_out)] = p_in_h + target_p;
}
```

### RU19A (APU)

Biggest multi-domain component. Electrical + Mechanical + Thermal.

#### Test (RED)

```cpp
TEST(PushComponents, RU19AStartSequence) {
    SimulationState st;
    uint32_t v_start = st.allocate_signal(28.0f, {Domain::Electrical, false});
    uint32_t v_bus = st.allocate_signal(0.0f, {Domain::Electrical, false});
    uint32_t k_mod = st.allocate_signal(0.5f, {Domain::Logical, false});
    uint32_t t4_out = st.allocate_signal(20.0f, {Domain::Thermal, false});
    uint32_t rpm_out = st.allocate_signal(0.0f, {Domain::Mechanical, false});

    RU19A<JitProvider> apu;
    apu.provider.set(PortNames::v_start, v_start);
    apu.provider.set(PortNames::v_bus, v_bus);
    apu.provider.set(PortNames::k_mod, k_mod);
    apu.provider.set(PortNames::t4_out, t4_out);
    apu.provider.set(PortNames::rpm_out, rpm_out);
    apu.auto_start = true;
    apu.pre_load();

    // Run 1 second of simulation
    for (int i = 0; i < 60; i++) {
        apu.solve_electrical(st, 1.0f / 60.0f);
        if (i % 3 == 0) apu.solve_mechanical(st, 1.0f / 20.0f);
        apu.finalize_step(st, 1.0f / 60.0f);
    }

    // APU should be cranking, RPM should be rising
    EXPECT_GT(apu.current_rpm, 0.0f);
}
```

#### Implementation (GREEN)

```cpp
template <typename Provider>
void RU19A<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float rpm_pct = current_rpm * inv_target_rpm;

    // Generator mode: output voltage
    float gen_mask = (this->state == APUState::RUNNING) ? 1.0f : 0.0f;
    float phi = std::clamp((rpm_pct - 0.4f) * 5.0f, 0.0f, 1.0f);
    float k_mod_val = st.values[provider.get(PortNames::k_mod)];
    float v_gen = 28.5f * phi * k_mod_val;
    st.values[provider.get(PortNames::v_bus)] = v_gen * gen_mask;

    // Starter mode: load on starter bus (doesn't modify voltage, just reads it)
    // Current draw is internal state, not written to values[]
}
```

Mechanical and thermal `solve_*()` methods: same logic as before but `st.across` -> `st.values`.

---

## Step 2.7: Splitter, Merger, Bus, BlueprintInput, BlueprintOutput

These are all no-ops in push architecture. Just rename:

```cpp
template <typename Provider>
void Splitter<Provider>::solve_electrical(SimulationState& /*st*/, float /*dt*/) {}
// etc. for all domains and all pass-through components
```

---

## Step 2.8: Methods to DELETE from Components

After migration, these methods no longer exist on any component:

| Method | Reason |
|--------|--------|
| `stamp_electrical_passive()` | legacy stamping |
| `stamp_electrical_actuator()` | legacy stamping |
| `observe_electrical()` | legacy post-solve observation |
| `commit_control()` | legacy control commit phase - merge into `execute()` or `post_step()` |

The `commit_control()` logic for Switch/Relay/HoldButton/AZS moves into their `solve_electrical()` method since there's no legacy phase separation anymore.

Remove from `all.h` declarations and `all.cpp` implementations.

---

## Step 2.9: Global Rename `st.across` -> `st.values`

After all components are migrated, do a global find-and-replace in `all.cpp`:

```
st.across[  ->  st.values[
st.through[ ->  (DELETE the entire line or rework)
st.conductance[ -> (DELETE the entire line)
```

Also update:
- `simulator.cpp` (get_wire_voltage, apply_overrides)
- `jit_solver.cpp` (build_systems_dev signal initialization)
- All test files

---

## Files Changed Summary

| File | Action |
|------|--------|
| `src/jit_solver/components/all.h` | Remove legacy-specific methods, update field types |
| `src/jit_solver/components/all.cpp` | Rewrite all solve methods for push, delete stamp methods |
| `src/jit_solver/state.h` | Already done in Phase 1 |
| `tests/test_push_components.cpp` | NEW: component-level push tests |
| `tests/CMakeLists.txt` | Add test_push_components target |

## Completion Criteria

- [ ] Every component's `solve_*()` method reads from `st.values[]` and writes to `st.values[]`
- [ ] No reference to `st.across`, `st.through`, `st.conductance` anywhere in codebase
- [ ] No `stamp_*()` calls anywhere in codebase
- [ ] `observe_electrical()` deleted from all components
- [ ] `stamp_electrical_actuator()` deleted from all components
- [ ] `commit_control()` merged into `solve_electrical()` for Switch/Relay/HoldButton/AZS
- [ ] All component tests pass: `cd build && ctest -R push_components`
- [ ] `cd build && ctest` reports 0 failures across ALL tests
