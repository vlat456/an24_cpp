# Simulation Engine

## SimulationState (SoA Arrays)

The core simulation state uses Structure of Arrays for cache-friendly iteration:

```cpp
struct SimulationState {
    alignas(64) std::vector<float> across;      // Potentials (V, P, T)
    alignas(64) std::vector<float> through;     // Flows (I, Q, H)
    alignas(64) std::vector<float> conductance; // Accumulated G
    alignas(64) std::vector<float> inv_conductance; // Precomputed 1/G
    
    uint32_t dynamic_signals_count = 0;  // Iterate only up to this!
    
    uint32_t allocate_signal(float initial, SignalType type);
    void clear_through();
    void precompute_inv_conductance();
};
```

### Signal Layout
```
[0 .. dynamic_signals_count-1] → Dynamic signals (solved each iteration)
[dynamic_signals_count .. size-1] → Fixed signals (boundary conditions)
```

This enables **branchless iteration** - no `is_fixed` checks needed!

## Push Propagation Model

Single-pass push model updates all potentials directly:
```cpp
// Components write outputs directly to signal arrays during execute()
template<typename Provider>
void MyComponent<Provider>::execute(SimulationState& st, float dt) {
    float input = st.across[provider.get(PortNames::v_in)];
    st.across[provider.get(PortNames::v_out)] = compute_output(input);
}
```

No iterative solver - components self-contained with direct signal propagation.

## Stamping Helpers (Legacy)

Optimized inline functions for common circuit patterns:

### Two-Port Conductance
```cpp
void stamp_two_port(conductance, through, across, idx1, idx2, g);
// Stamps: through[idx1] += (V2-V1)*g, conductance[idx1] += g
```

### One-Port to Ground
```cpp
void stamp_one_port_ground(conductance, through, across, idx, g);
// Stamps: through[idx] -= V*g, conductance[idx] += g
```

### Voltage Source (Thevenin → Norton)
```cpp
void stamp_voltage_source(conductance, through, across, idx, V_source, R_internal);
// Converts to: I_source = V/R, G = 1/R
```

### Current Source
```cpp
void stamp_current_source(conductance, through, idx, g, i_source);
// Stamps: conductance[idx] += g, through[idx] += i_source
```

## Phase Scheduling

Components are sorted by explicit execution phase at build time:

```cpp
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

### Step Pipeline (dt-driven)

Runtime executes explicit phases in one outer `step(dt)`:

1. `execute_electrical_passive`
2. `execute_electrical_observer`
3. `execute_logical`
4. `execute_control_commit`
5. `execute_electrical_actuator`
6. sub-rate domain ticks (`mechanical`, `hydraulic`, `thermal`)
7. `finalize_step`

`dt <= 0` is pause/no-advance: no phase advances simulation state.

### Sub-rate Domains (period-based, monitor-agnostic)

Slow domains use accumulated simulation time, not frame counters or display refresh:

- mechanical period: `1/20` s
- hydraulic period: `1/5` s
- thermal period: `1` s

Each domain uses bounded catch-up loops for large `dt` with optional cap logging.
Outputs are latched between ticks and slow domains read final post-actuator electrical state.

These are simulation periods, not monitor frame rates. Calling `step(dt)` at 50/60/144/200 Hz
or variable cadence yields equivalent behavior over equal simulated time.

## BuildResult

Result of JIT build process:
```cpp
struct BuildResult {
    uint32_t signal_count;
    std::vector<uint32_t> fixed_signals;
    std::unordered_map<std::string, uint32_t> port_to_signal;
    std::unordered_map<std::string, ComponentVariant> devices;
    PhaseComponents phase_components;
    std::vector<float> lut_keys;
    std::vector<float> lut_values;
};
```

## Convergence Checking

```cpp
state.save_convergence_state();  // Copy across[] to buffer
// ... run iterations ...
bool converged = state.has_converged(tolerance);  // Compare max change
```

## Domain Scheduling Constants

Embedded in codegen for AOT and in `DomainSchedule` namespace for JIT:
- `MECHANICAL_PERIOD = 3` - 20 Hz = every 3rd step at 60 Hz
- `HYDRAULIC_PERIOD = 12` - 5 Hz = every 12th step at 60 Hz
- `THERMAL_PERIOD = 60` - 1 Hz = every 60th step at 60 Hz
- `CYCLE_LENGTH = 60` - Least common multiple of all periods
