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

## SOR Solver (Successive Over-Relaxation)

Single iteration updates all potentials:
```cpp
AOT_ALWAYS_INLINE void solve_sor_iteration(
    float* across, const float* through, 
    const float* inv_conductance, size_t count, float omega
) {
    for (size_t i = 0; i < count; ++i) {
        across[i] += through[i] * inv_conductance[i] * omega;
    }
}
```

## Norton Stamping Helpers

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

## Domain Scheduling

Components are sorted by domain at build time for branchless iteration:

```cpp
struct DomainComponents {
    std::vector<ComponentVariant> electrical;  // 60 Hz
    std::vector<ComponentVariant> logical;     // 60 Hz
    std::vector<ComponentVariant> mechanical;  // 20 Hz
    std::vector<ComponentVariant> hydraulic;   // 5 Hz
    std::vector<ComponentVariant> thermal;     // 1 Hz
};
```

### Step Multipliers
| Domain | Every N steps | Effective dt |
|--------|---------------|--------------|
| Electrical | 1 | 1/60 sec |
| Logical | 1 | 1/60 sec |
| Mechanical | 3 | 3/60 = 1/20 sec |
| Hydraulic | 12 | 12/60 = 1/5 sec |
| Thermal | 60 | 60/60 = 1 sec |

## BuildResult

Result of JIT build process:
```cpp
struct BuildResult {
    std::vector<DeviceInstance> devices;
    std::vector<Connection> connections;
    std::unordered_map<std::string, uint32_t> port_to_signal;
    uint32_t signal_count;
    DomainComponents components;  // Sorted by domain
    SimulationState state;
};
```

## Convergence Checking

```cpp
state.save_convergence_state();  // Copy across[] to buffer
// ... run iterations ...
bool converged = state.has_converged(tolerance);  // Compare max change
```

## SOR Constants

Located in `src/jit_solver/SOR_constants.h`:
- `omega` - Relaxation factor (typically 1.2-1.5)
- `max_iterations` - Maximum SOR iterations per frame
- `tolerance` - Convergence threshold
