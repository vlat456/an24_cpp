# C++ Component Parameter Analysis

This document provides a comprehensive breakdown of parameter loading, storage, and usage for 10 key components.

## Quick Key Findings

- **All params are loaded at init time** via `param_reader.consume_*()` in `jit_solver.cpp`
- **All components use public member variables** to store param values (not private with accessors)
- **Pre-computation:** Some components compute derived values in `pre_load()` (Normalize, ControlledVoltageSource)
- **Parameter access:** Params are either:
  1. Not accessed in execute/commit (solver-owned components like Resistor, CurrentSense)
  2. Read from member variables in execute/commit (logical/control components like VoltageSense, Clamp, PI)

---

## 1. Resistor

**Header:** `src/jit_solver/components/resistor.h`

**Member Variables (param storage):**
- `float conductance = 0.1f`

**Parameter Loading:**
- **Method:** `param_reader.consume_float_optional("conductance", 0.1f);`
- **Timing:** Load-time only
- **Code Location:** `jit_solver.cpp` in component factory

**Usage Pattern:**
- Not used in execute/commit (solver-owned component)
- Conductance only referenced by electrical solver during network solve

**Port Names Used:**
- None directly in component code (solver-owned)

---

## 2. ControlledVoltageSource

**Header:** `src/jit_solver/components/controlled_voltage_source.h`

**Member Variables (param storage):**
- `float gain = 1.0f`
- `float offset = 0.0f`
- `float min_v = 0.0f`
- `float max_v = 30.0f`
- `float r_internal = 0.1f`
- `float inv_r = 10.0f` ← precomputed in `pre_load()`

**Parameter Loading:**
```cpp
comp.gain = param_reader.consume_float_optional("gain", 1.0f);
comp.offset = param_reader.consume_float_optional("offset", 0.0f);
comp.min_v = param_reader.consume_float_optional("min_v", 0.0f);
comp.max_v = param_reader.consume_float_optional("max_v", 30.0f);
comp.r_internal = param_reader.consume_float_optional("r_internal", 0.1f);
comp.pre_load();  // Computes inv_r
```

**pre_load() computation:**
```cpp
float safe_r = std::max(r_internal, 1e-9f);
inv_r = 1.0f / safe_r;
```

**Usage Pattern:**
- Params NOT used in execute/commit code directly
- Solver uses gain/offset/min_v/max_v to compute dynamic source voltage
- commit() only reads solved branch current

**Port Names Used:**
- `i_out` (output current in commit)

---

## 3. CurrentSense

**Header:** `src/jit_solver/components/current_sense.h`

**Member Variables (param storage):**
- `float conductance = 1000.0f`

**Parameter Loading:**
```cpp
comp.conductance = param_reader.consume_float_optional("conductance", 1000.0f);
```

**Usage Pattern:**
- Not used in execute/commit code
- Solver-owned, execute() only reads solved branch current

**Port Names Used:**
- `i_out` (output in execute)

---

## 4. VariableConductance

**Header:** `src/jit_solver/components/variable_conductance.h`

**Member Variables (param storage):**
- `float g_min = 0.001f`
- `float g_max = 10.0f`

**Parameter Loading:**
```cpp
comp.g_min = param_reader.consume_float_optional("g_min", 0.001f);
comp.g_max = param_reader.consume_float_optional("g_max", 10.0f);
```

**Usage Pattern:**
- Not used in execute/commit (both empty)
- Params used by simulator's `update_dynamic_sources()` for electrical solver
- Solver interpolates conductance: `g = lerp(g_min, g_max, cmd)`

**Port Names Used:**
- None directly in component code

---

## 5. VoltageSense

**Header:** `src/jit_solver/components/voltage_sense.h`

**Member Variables (param storage):**
- `float gain = 1.0f`
- `float offset = 0.0f`

**Parameter Loading:**
```cpp
comp.gain = param_reader.consume_float_optional("gain", 1.0f);
comp.offset = param_reader.consume_float_optional("offset", 0.0f);
```

**Usage Pattern in execute():**
```cpp
float v = st.values[provider.get(PortNames::v_in)];
float vref = st.values[provider.get(PortNames::v_ref)];
st.values[provider.get(PortNames::out)] = (v - vref) * gain + offset;
```
- Both `gain` and `offset` are **directly accessed** in execute()

**Port Names Used:**
- `v_in` (input voltage)
- `v_ref` (reference voltage)
- `out` (output)

---

## 6. Clamp

**Header:** `src/jit_solver/components/clamp.h`

**Member Variables (param storage):**
- `float min = 0.0f`
- `float max = 1.0f`

**Parameter Loading:**
```cpp
comp.min = param_reader.consume_float_optional("min", 0.0f);
comp.max = param_reader.consume_float_optional("max", 1.0f);
```

**Usage Pattern in execute():**
```cpp
float input = st.values[in_idx];
st.values[out_idx] = std::clamp(input, min, max);
```
- Both `min` and `max` are **directly accessed** in execute()

**Port Names Used:**
- `in` (input)
- `out` (output)

---

## 7. Normalize

**Header:** `src/jit_solver/components/normalize.h`

**Member Variables (param storage):**
- `float min = 0.0f`
- `float max = 100.0f`
- `float inv_range = 0.01f` ← precomputed in `pre_load()`

**Parameter Loading:**
```cpp
comp.min = param_reader.consume_float_optional("min", 0.0f);
comp.max = param_reader.consume_float_optional("max", 100.0f);
comp.pre_load();  // Computes inv_range
```

**pre_load() computation:**
```cpp
float range = max - min;
inv_range = (std::abs(range) > 1e-6f) ? (1.0f / range) : 0.0f;
```

**Usage Pattern in execute():**
```cpp
float input = st.values[in_idx];
float normalized = (input - min) * inv_range;
st.values[out_idx] = std::clamp(normalized, 0.0f, 1.0f);
```
- Uses `min` and `inv_range` (precomputed)

**Port Names Used:**
- `in` (input)
- `out` (output)

---

## 8. Integrator

**Header:** `src/jit_solver/components/integrator.h`

**Member Variables (param storage):**
- `float gain = 1.0f`
- `float initial_val = 0.0f`

**State Variables (NOT params, but initialized from params):**
- `double accumulator = 0.0` (committed state, initialized to initial_val)
- `double next_accumulator = 0.0` (staged state)
- `float first_frame_mask = 1.0f` (committed state)
- `float next_first_frame_mask = 1.0f` (staged state)

**Parameter Loading:**
```cpp
comp.gain = param_reader.consume_float_required("gain");
comp.initial_val = param_reader.consume_float_required("initial_val");
comp.accumulator = comp.initial_val;  // Initialize state from param
comp.next_accumulator = comp.initial_val;
```

**Usage Pattern in execute():**
```cpp
float val_in = st.values[in_idx];
float reset_in = st.values[reset_idx];

float committed_acc = accumulator + (initial_val - accumulator) * first_frame_mask;
float integrated = committed_acc + val_in * gain * dt;  // gain used here
float new_accumulator = (reset_in > 0.5f) ? 0.0f : integrated;

next_accumulator = new_accumulator;
next_first_frame_mask = 0.0f;
st.values[out_idx] = committed_acc;
```
- Uses `gain` and `initial_val` in execute()

**Usage Pattern in commit():**
```cpp
accumulator = next_accumulator;
first_frame_mask = next_first_frame_mask;
```

**Port Names Used:**
- `in` (input)
- `reset` (reset signal)
- `out` (output)

---

## 9. PI

**Header:** `src/jit_solver/components/pi.h`

**Member Variables (param storage):**
- `float Kp = 1.0f`
- `float Ki = 0.0f`
- `float output_min = -1000.0f`
- `float output_max = 1000.0f`

**State Variables (NOT params):**
- `double integral = 0.0` (accumulated error integral, not a param)

**Parameter Loading:**
```cpp
comp.Kp = param_reader.consume_float_required("Kp");
comp.Ki = param_reader.consume_float_required("Ki");
comp.output_min = param_reader.consume_float_required("output_min");
comp.output_max = param_reader.consume_float_required("output_max");
```

**Usage Pattern in execute():**
```cpp
float sp = st.values[provider.get(PortNames::setpoint)];
float fb = st.values[provider.get(PortNames::feedback)];
float error = sp - fb;

integral += error * safe_dt;

float output = Kp * error + Ki * integral;  // All 4 params used
output = std::clamp(output, output_min, output_max);

// Integral anti-windup using Ki
if (std::abs(Ki) > 1e-9f) {
    float i_lo = output_min / Ki;
    float i_hi = output_max / Ki;
    if (i_lo > i_hi) std::swap(i_lo, i_hi);
    integral = std::clamp(integral, static_cast<double>(i_lo), static_cast<double>(i_hi));
}

st.values[provider.get(PortNames::output)] = output;
```
- All four params are **directly accessed** in execute()

**Port Names Used:**
- `setpoint` (setpoint input)
- `feedback` (feedback input)
- `output` (output)

---

## 10. Relay

**Header:** `src/jit_solver/components/relay.h`

**Member Variables (param storage):**
- `bool closed = false`
- `float hold_threshold = 0.5f`
- `float g_open = 1e-6f`
- `float g_closed = 1000.0f`

**Parameter Loading:**
```cpp
comp.closed = param_reader.consume_bool_optional("closed", false);
comp.hold_threshold = param_reader.consume_float_optional("hold_threshold", 0.5f);
comp.g_open = param_reader.consume_float_optional("g_open", 1e-6f);
comp.g_closed = param_reader.consume_float_optional("g_closed", 1000.0f);
```

**Usage Pattern in commit_control():**
```cpp
float control = st.values[provider.get(PortNames::control)];

if (control > hold_threshold) {  // hold_threshold used
    closed = true;
} else if (control < -hold_threshold) {
    closed = false;
}

st.values[provider.get(PortNames::state)] = closed ? 1.0f : 0.0f;
```
- `hold_threshold` is **directly accessed** in commit_control()
- `closed` is state variable (modified by commit_control)
- `g_open` and `g_closed` used by solver for conductance switching

**Port Names Used:**
- `control` (control input)
- `state` (state output)

---

## Summary Table

| Component | Param Member Vars | Load Method | Access Pattern | Precomputed? | Port Names |
|-----------|-------------------|-------------|-----------------|--------------|-----------|
| **Resistor** | `conductance` | optional | None (solver-owned) | No | — |
| **ControlledVoltageSource** | `gain`, `offset`, `min_v`, `max_v`, `r_internal` | optional | None (solver uses) | Yes (`inv_r`) | `i_out` |
| **CurrentSense** | `conductance` | optional | None (solver-owned) | No | `i_out` |
| **VariableConductance** | `g_min`, `g_max` | optional | None (solver uses) | No | — |
| **VoltageSense** | `gain`, `offset` | optional | In execute() | No | `v_in`, `v_ref`, `out` |
| **Clamp** | `min`, `max` | optional | In execute() | No | `in`, `out` |
| **Normalize** | `min`, `max` | optional + pre_load | In execute() | Yes (`inv_range`) | `in`, `out` |
| **Integrator** | `gain`, `initial_val` | required | In execute() | No | `in`, `reset`, `out` |
| **PI** | `Kp`, `Ki`, `output_min`, `output_max` | required | In execute() | No | `setpoint`, `feedback`, `output` |
| **Relay** | `closed`, `hold_threshold`, `g_open`, `g_closed` | optional | In commit_control() | No | `control`, `state` |

---

## Parameter Loading Patterns

### By Category

**Required Parameters (must be in JSON):**
- Integrator: `gain`, `initial_val`
- PI: `Kp`, `Ki`, `output_min`, `output_max`

**Optional Parameters (use defaults):**
- All others use `.consume_float_optional()` with sensible defaults

### Pre-computation

Two components compute derived values in `pre_load()`:
1. **Normalize**: Computes `inv_range = 1.0f / (max - min)` to avoid division in tight loop
2. **ControlledVoltageSource**: Computes `inv_r = 1.0f / r_internal` for solver

### Access Timing

- **All params loaded at init time** (no runtime param updates)
- **Static access:** Solver-owned components don't access params in execute/commit
- **Dynamic access:** Logical components read params directly in execute/commit

