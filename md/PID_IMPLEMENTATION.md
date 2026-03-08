# PID Controller Implementation Plan

## Overview

Implement a branchless, time-invariant PID controller component for the AN-24 flight simulator. The PID will follow the existing component pattern (Provider template, `solve_electrical` + `post_step`) while using efficient branchless math.

---

## Component Specification

### JSON Definition

**File:** `components/PID.json`

```json
{
  "classname": "PID",
  "description": "PID controller with proportional, integral, and derivative terms",
  "default_ports": {
    "setpoint": {
      "direction": "In",
      "type": "Any"
    },
    "feedback": {
      "direction": "In",
      "type": "Any"
    },
    "output": {
      "direction": "Out",
      "type": "Any"
    }
  },
  "default_params": {
    "Kp": "1.0",
    "Ki": "0.0",
    "Kd": "0.0",
    "output_min": "-1000.0",
    "output_max": "1000.0",
    "filter_alpha": "0.2"
  },
  "default_domains": [
    "Electrical"
  ],
  "default_priority": "med",
  "default_critical": false
}
```

**Ports:**
| Port | Direction | Description |
|------|-----------|-------------|
| `setpoint` | In | Target value (e.g., desired voltage) |
| `feedback` | In | Process variable (e.g., measured voltage) |
| `output` | Out | Control signal (e.g., excitation current) |

**Parameters:**
| Param | Default | Description |
|-------|---------|-------------|
| `Kp` | 1.0 | Proportional gain |
| `Ki` | 0.0 | Integral gain |
| `Kd` | 0.0 | Derivative gain |
| `output_min` | -1000.0 | Minimum output (anti-windup) |
| `output_max` | 1000.0 | Maximum output (anti-windup) |
| `filter_alpha` | 0.2 | D-term low-pass filter coefficient (0-1) |

---

## C++ Implementation

### Class Declaration

**File:** `src/jit_solver/components/all.h` (add after `LerpNode`)

```cpp
/// PID - Proportional-Integral-Derivative controller
template <typename Provider = JitProvider>
class PID {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float Kp = 1.0f;
    float Ki = 0.0f;
    float Kd = 0.0f;
    float output_min = -1000.0f;
    float output_max = 1000.0f;
    float filter_alpha = 0.2f;

    // State variables (minimal footprint: 3 floats)
    float integral = 0.0f;
    float last_error = 0.0f;
    float d_filtered = 0.0f;

    PID() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
    void post_step(an24::SimulationState& st, float dt);
};
```

**State footprint:** Only **12 bytes** per PID instance (3 floats). No pollution.

---

### Implementation

**File:** `src/jit_solver/components/all.cpp`

```cpp
template <typename Provider>
void PID<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // Electrical pass: set high output impedance
    float g_out = 1e-6f;  // 1MΩ output impedance (virtual control signal)
    st.conductance[provider.get(PortNames::output)] += g_out;
}

template <typename Provider>
void PID<Provider>::post_step(an24::SimulationState& st, float dt) {
    // Read inputs
    float setpoint = st.across[provider.get(PortNames::setpoint)];
    float feedback = st.across[provider.get(PortNames::feedback)];

    // ===== Time-invariant, branchless PID =====

    // 1. Clamp dt (protect against lag spikes and division by zero)
    // Using std::max/min which compile to branchless CMOV/SEL instructions
    float safe_dt = std::max(1e-6f, std::min(dt, 0.1f));
    float inv_dt = 1.0f / safe_dt;  // Single division for entire step

    // 2. Compute error
    float error = setpoint - feedback;

    // 3. Proportional term (time-invariant)
    float p_term = Kp * error;

    // 4. Integral term with clamping (anti-windup)
    // Accumulate: integral += error * dt
    integral += error * safe_dt;

    // Clamp integral to prevent windup (branchless)
    // When output saturates, we limit integral contribution
    float i_term_unclamped = Ki * integral;
    float i_term = std::clamp(i_term_unclamped,
                              output_min - p_term,
                              output_max - p_term);

    // 5. Derivative term with low-pass filter
    // Raw derivative: d = (error - last_error) / dt
    float d_raw = (error - last_error) * inv_dt;

    // Low-pass filter: d_filtered += alpha * (d_raw - d_filtered)
    // This is branchless LERP: d = d + α*(new - d)
    d_filtered += filter_alpha * (d_raw - d_filtered);
    float d_term = Kd * d_filtered;

    // 6. Compute raw output
    float raw_output = p_term + i_term + d_term;

    // 7. Output saturation (branchless clamp)
    float output = std::clamp(raw_output, output_min, output_max);

    // 8. Write output
    st.across[provider.get(PortNames::output)] = output;

    // 9. Update state
    last_error = error;
}
```

---

## Key Design Decisions

### 1. Minimal State Footprint
Only **3 floats** stored per PID instance:
- `integral` - accumulated error
- `last_error` - previous error for derivative
- `d_filtered` - filtered derivative value

No need to store `Ki_d` or `Kd_d` (discrete coefficients) because we compute them inline with `safe_dt` and `inv_dt`.

### 2. Time Invariance
The PID produces identical control behavior regardless of frame rate:
- On 60Hz: `safe_dt ≈ 0.0167s`, larger steps
- On 144Hz: `safe_dt ≈ 0.0069s`, smaller steps but more frequent
- Over 1 second: integral accumulates to same value

### 3. Branchless Operation
All operations are branchless (compile to CMOV/MINSS/MAXSS):
- `std::max(1e-6f, std::min(dt, 0.1f))` - dt clamping
- `std::clamp(...)` - output saturation
- `d_filtered += alpha * (d_raw - d_filtered)` - LERP filter

### 4. Anti-Windup
Two-level protection:
1. **Integral clamping** - limits integral contribution based on available output headroom
2. **Output clamping** - final saturation to `[output_min, output_max]`

### 5. D-Term Filtering
The derivative term is low-pass filtered to prevent:
- High-frequency noise amplification
- Numerical instability at high frame rates

---

## Implementation Steps

### Phase 1: Core Implementation (2-3 hours)

| Step | Task | File |
|------|------|------|
| 1.1 | Add `PID` class to `all.h` | `src/jit_solver/components/all.h` |
| 1.2 | Implement `solve_electrical` and `post_step` | `src/jit_solver/components/all.cpp` |
| 1.3 | Add explicit instantiation | `src/jit_solver/components/explicit_instantiations.h` |
| 1.4 | Add to `all_components` list | `src/jit_solver/port_registry.h` |

### Phase 2: JSON Definition (10 minutes)

| Step | Task | File |
|------|------|------|
| 2.1 | Create `PID.json` | `components/PID.json` |

### Phase 3: Testing (1-2 hours)

| Step | Test | Expected Result |
|------|------|-----------------|
| 3.1 | Static test (dt=constant) | Step response matches theory |
| 3.2 | Varying dt test | Same response regardless of dt |
| 3.3 | Windup test | Integral doesn't grow when saturated |
| 3.4 | Noise test | D-term doesn't amplify high-freq noise |

---

## Testing Strategy

### Unit Test: Basic Step Response

```cpp
TEST(PIDTest, ProportionalOnly) {
    // Kp=2, Ki=0, Kd=0
    // setpoint=10, feedback=0
    // Expected: output = 2 * (10 - 0) = 20
}

TEST(PIDTest, IntegralAccumulates) {
    // Kp=0, Ki=1, Kd=0
    // setpoint=5, feedback=0
    // After 1s at 60Hz: output = 5 * 1.0 = 5.0
    // After 1s at 144Hz: output = 5 * 1.0 = 5.0 (same!)
}

TEST(PIDTest, DerivativeFiltersNoise) {
    // Kp=0, Ki=0, Kd=1
    // Add high-freq noise to feedback
    // Verify d_filtered has lower amplitude than raw derivative
}

TEST(PIDTest, AntiWindup) {
    // Kp=1, Ki=10, output_max=10
    // setpoint=100, feedback=0
    // Verify integral doesn't grow unbounded
    // Verify output clamps at 10
}

TEST(PIDTest, TimeInvariance) {
    // Same PID, different dt values
    // Run for 1 second with dt=0.016 (60Hz)
    // Run for 1 second with dt=0.007 (144Hz)
    // Outputs should match within numerical precision
}
```

---

## Example Usage

### Electrical System: Voltage Regulator

```
[Battery 28V] --> [Bus] --> [Load]
                    ^
                    |
                 [PID]
                    +-- setpoint: 28V
                    +-- feedback: bus voltage
                    +-- output --> [Generator Field]
```

**Parameters for voltage regulation:**
```json
{
  "Kp": "10.0",
  "Ki": "50.0",
  "Kd": "0.1",
  "output_min": "0.0",
  "output_max": "5.0"
}
```

### Thermal System: Temperature Control

```
[Heater] --> [Thermal Mass] --> [TempSensor]
              ^
              |
           [PID]
              +-- setpoint: 60°C
              +-- feedback: temp sensor
              +-- output --> heater power
```

---

## Performance Notes

### Memory Layout (SoA-Friendly)

For batch SIMD processing (future optimization), PID state can be rearranged:

```cpp
struct PIDBatch {
    // Arrays of 4/8 PIDs for SIMD processing
    float Kp[N], Ki[N], Kd[N];
    float integral[N], last_error[N], d_filtered[N];
};
```

**Current implementation** is already SIMD-friendly:
- All operations are scalar → auto-vectorizable by compiler
- Data access is sequential in port arrays
- No branching in hot path

### Compiler Flags for Optimization

Ensure these flags are set for maximum performance:
```
-O3                # Maximum optimization
-ffast-math        # Allow aggressive floating-point optimizations
-march=native      # Use CPU-specific instructions (AVX2/AVX-512)
-funroll-loops     # Unroll small loops
```

---

## Success Criteria

- [ ] `PID` class compiles without warnings
- [ ] Component appears in editor component registry
- [ ] Unit tests pass for P, I, D terms individually
- [ ] Time invariance verified across different dt values
- [ ] Anti-windup prevents integral saturation
- [ ] D-term filtering reduces noise amplification
- [ ] No regression in existing component tests
- [ ] Integration test: PID regulates voltage in simple circuit

---

## Files to Modify/Create

```
components/PID.json                          (NEW)
src/jit_solver/components/all.h              (MODIFY: add PID class)
src/jit_solver/components/all.cpp            (MODIFY: add PID impl)
src/jit_solver/components/explicit_instantiations.h  (MODIFY: add template class PID)
src/jit_solver/port_registry.h               (MODIFY: register PID)
tests/test_pid.cpp                           (NEW: unit tests)
```

---

## Future Enhancements (Out of Scope)

1. **Feedforward term** - Add `feedforward` input for predictive control
2. **Bumpless transfer** - Smooth transition when changing setpoints
3. **Gain scheduling** - Different Kp/Ki/Kd based on operating region
4. **PID auto-tuning** - Ziegler-Nichols or Cohen-Coon methods
5. **Batch SIMD** - Process multiple PIDs in parallel (if >4 instances)

---

## References

- [PID Controller Theory](https://en.wikipedia.org/wiki/PID_controller)
- [Anti-Windup Strategies](https://www.controleng.com/articles/anti-windup-control/)
- [Low-Pass Filter Design](https://en.wikipedia.org/wiki/Low-pass_filter)
- Existing components: `components/LerpNode.json`, `src/jit_solver/components/all.h`
