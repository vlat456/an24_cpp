# An-24 Simulation Project Context

## Overview
This is a C++ flight simulation project for An-24 aircraft, focusing on the electrical system simulation including VSU (Auxiliary Power Unit), generators, and battery management.

## Project Structure
- `/Users/vladimir/an24_cpp` - C++ simulation (main branch)
- `/Users/vladimir/an24_5` - Rust version (experimental branch)

## Key Components Implemented

### 1. RU19A-300 (ВСУ) - Auxiliary Power Unit
Complete APU with state machine:
- **States**: OFF → CRANKING → IGNITION → RUNNING → STOPPING
- **Ports**:
  - `v_start` - starter power input (direct from battery, bypasses DMR)
  - `v_bus` - bus voltage output (goes through DMR)
  - `k_mod` - excitation modulation input (from RUG82)
  - `v_gen_mon` - generator voltage monitoring
  - `rpm_out` - RPM output signal
  - `t4_out` - T4 temperature output

**Parameters**:
| Parameter | Value |
|----------|-------|
| target_rpm | 16000 |
| idle_rpm | 9600 (60%) |
| R_internal (motor) | 0.025 Ohm |
| R_norton (generator) | 0.08 Ohm |
| k_motor (back-EMF) | 38.0 V at 100% |
| I_max generator | 400A (clamped to 100A) |
| crank_time | 2 sec |
| ignition_time | 3 sec |

### 2. RUG-82 Voltage Regulator
- Input: `v_gen` - generator voltage (bus voltage)
- Output: `k_mod` - excitation modulation (0...1)
- Maintains 28.5V on bus

### 3. DMR-400 Differential Minimum Relay
Connects generator to DC bus:
- **Closes when**: V_gen > V_bus + 2.0V AND V_gen > 20V
- **Opens when**: V_bus > V_gen + 10V (reverse current protection)
- Default state: open (false)
- Has reconnect delay: 1 second

### 4. GS24 Starter-Generator
Dual-mode electrical machine:
- **Motor mode**: consumes power, back-EMF reduces current as RPM increases
- **Generator mode**: Norton equivalent - produces current based on RPM and k_mod

### 5. LerpNode (Filter)
First-order filter for sensor simulation:
```cpp
output = output + factor * (input - output)
```
- factor = 0.1 means slow filter (~10 sec to settle)
- factor = 1.0 means instant

Used for T4 temperature sensor inertia.

## Important Bug Fixes

### 1. Back-EMF Too High
**Problem**: Initially used `back_emf = 0.5 * current_rpm`. At 16000 RPM this gave 8000V - 300x the battery voltage!

**Fix**: Use `back_emf = 38.0 * rpm_percent` - gives 38V at 100% RPM, matching real machine.

### 2. DMR-400 Startup Deadlock
**Problem**: Circular dependency - DMR won't close until generator runs, but starter can't get power through DMR.

**Fix**: Voltage-based detection:
- DMR closes when V_gen > 20V (generator is running)
- During CRANKING, generator consumes power, voltage stays low
- After transition to RUNNING, generator produces 28V → DMR closes

### 3. Starter Bypasses DMR
**Problem**: In An-24, starter current goes directly from batteries, not through DMR.

**Fix**: Two ports in RU19A:
- `v_start` - connected directly to battery
- `v_bus` - goes through DMR to bus

### 4. Realistic Voltage Drop
At startup, current 700-850A causes voltage drop to 17-21V (not 28V).

## Test Configurations

### vsu_test.json
Basic VSU test with direct battery connection:
- Battery → VSU → RUG82 → Light
- Shows: bus=28.5V, rpm=60%, k_mod=0.15

### vsu_dmr_test.json
VSU with DMR-400 relay:
- Battery → Bus → DMR → VSU (generator output)
- Starter: Battery → VSU (v_start)
- Shows: bus=28.1V, rpm=60%, k_mod=0.05

### lerp_test.json
Tests LerpNode filter behavior

### gs24_test.json
Tests standalone GS24 generator

## Code Locations

### C++ Components
- `/Users/vladimir/an24_cpp/src/jit_solver/components/all.h` - Component definitions
- `/Users/vladimir/an24_cpp/src/jit_solver/components/all.cpp` - Component implementations
- `/Users/vladimir/an24_cpp/src/jit_solver/jit_solver.cpp` - JIT builder

### AOT Codegen
- `/Users/vladimir/an24_cpp/src/codegen/codegen.cpp` - Code generator
- `/Users_cpp/generated/`/vladimir/an24 - Generated code

### Test Files
- `/Users/vladimir/an24_cpp/src/aircraft/vsu_test.json`
- `/Users/vladimir/an24_cpp/src/aircraft/vsu_dmr_test.json`
- `/Users/vladimir/an24_cpp/src/aircraft/lerp_test.json`
- `/Users/vladimir/an24_cpp/src/aircraft/gs24_test.json`

## Running Tests

### JIT (Just-in-time)
```bash
cd /Users/vladimir/an24_cpp/build/examples
./json_test /Users/vladimir/an24_cpp/src/aircraft/vsu_test.json
```

### AOT (Ahead-of-time)
```bash
cd /Users/vladimir/an24_cpp/build/examples
./vsu_aot_test
```

## Key Insights

1. **Norton Model**: Components use Norton equivalent (current source + parallel conductance) for circuit solving with SOR (Successive Over-Relaxation)

2. **State Machine**: Components have state machines in post_step(), run every frame (60 Hz)

3. **Thermal Solver**: Runs at 1 Hz (every 60 frames), used for T4 temperature

4. **Signal-based Communication**: Components communicate through signals (voltage nodes), no direct pointers

5. **DMR Logic**: Uses voltage comparison to detect when generator is ready (not starter state) - this is the correct approach for component decoupling

## Architecture Details

### Simulation Domains
The solver runs multiple domains at different frequencies:
- **Electrical**: 60 Hz (fastest - voltage/current dynamics)
- **Mechanical**: 20 Hz (3 Hz bucket system)
- **Hydraulic**: 5 Hz (12 Hz bucket system)
- **Thermal**: 1 Hz (60 Hz bucket system)

Components can implement multiple domains. Thermal solver is called every 60 frames for RU19A T4 simulation.

### Solver Algorithm: SOR (Successive Over-Relaxation)
Each solve step:
1. Clear `through[]` and `conductance[]` accumulators
2. All components add their Norton equivalent: `through[i] += I_source`, `conductance[i] += G`
3. Solve: `V[i] = (through[i] * inv_conductance[i])` for non-fixed signals
4. Fixed signals (ground, RefNode with value=0) stay constant

Components use Norton model: current source + parallel conductance.

### Signal System
- `SimulationState` uses Structure of Arrays (SoA): `across[]`, `through[]`, `conductance[]`, `inv_conductance[]`, `signal_types[]`
- Ports map to signal indices via `PortToSignal` map
- Unconnected ports get sentinel index (last signal)
- Ground reference: RefNode with `value=0.0` is marked as fixed signal

### Port Connection: Union-Find
Connections between ports are resolved using Union-Find algorithm:
1. All device ports are added to the set
2. Connections (e.g., "battery.v_out -> vsu.v_start") unite the sets
3. Each connected set becomes one signal (voltage node)
4. Unique roots are remapped to 0-based indices

### JIT vs AOT Modes
- **JIT**: Components created dynamically from JSON at runtime, solved via reflection
- **AOT**: CodeGen generates C++ source from JSON, compiled ahead-of-time for performance

### JSON Configuration Format
```json
{
  "devices": [
    {
      "name": "battery",
      "classname": "Battery",
      "ports": { "v_in": "In", "v_out": "Out" },
      "params": { "v_nominal": "28.0", "internal_r": "0.01" },
      "domain": "Electrical"
    }
  ],
  "connections": [ "battery.v_out -> vsu.v_start" ]
}
```

### All Available Components
| Component | Domain | Description |
|-----------|--------|-------------|
| Battery | Electrical | Voltage source with internal resistance |
| Relay | Electrical | On/off switch (closed = wire) |
| Resistor | Electrical | Pure conductance element |
| Load | Electrical | Single-port load to ground |
| RefNode | Electrical | Fixed voltage reference |
| Bus | Electrical | Wire junction |
| Generator | Electrical | Voltage source like battery |
| GS24 | Electrical | Starter-Generator (dual mode) |
| Transformer | Electrical | AC transformer |
| Inverter | Electrical | DC-AC converter |
| LerpNode | Electrical | First-order filter (sensor inertia) |
| IndicatorLight | Electrical | Aircraft light with brightness output |
| HighPowerLoad | Electrical | Constant power load |
| Gyroscope | Electrical | Power-only sensor |
| AGK47 | Electrical | Attitude gyro |
| ElectricPump | Electrical+Hydraulic | Motor-driven pump |
| SolenoidValve | Hydraulic | Electrically controlled valve |
| InertiaNode | Mechanical | Mechanical inertia |
| TempSensor | Thermal | Temperature sensor |
| ElectricHeater | Electrical+Thermal | Heater element |
| Radiator | Thermal | Heat exchanger |
| RUG82 | Electrical | Voltage regulator (PI controller) |
| DMR400 | Electrical | Differential minimum relay |
| RU19A | Electrical+Thermal | APU with full start sequence |

### Code Structure
```
src/
├── json_parser/       # JSON parsing
│   ├── json_parser.h  # Data structures (DeviceInstance, Connection, etc.)
│   └── json_parser.cpp
├── jit_solver/        # Runtime solver
│   ├── component.h    # Base Component class
│   ├── state.h/cpp    # SimulationState (SoA arrays)
│   ├── systems.h/cpp  # Domain buckets and solve_step()
│   ├── jit_solver.h/cpp # Component factory + Union-Find
│   └── components/
│       ├── all.h      # All component class definitions
│       └── all.cpp    # All component implementations (~737 lines)
└── codegen/           # AOT code generation
    ├── codegen.h
    └── codegen.cpp    # Generates Systems class from JSON

## AOT Codegen Optimizations (ECS-like)

The C++ AOT code generator now produces highly optimized code similar to Rust version:

### 1. Jump Table Dispatch
```cpp
void Systems::solve_step(void* state, uint32_t step) {
    switch (step % 60) {
        case 0: step_0(state); break;
        case 1: step_1(state); break;
        // ... 60 cases
    }
}
```

### 2. Pre-generated Step Methods
Each step method contains direct inline calls to components:
```cpp
inline void Systems::step_0(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    // ...
}
```

### 3. Domain Scheduling
- Electrical: every step (60 Hz)
- Mechanical: every 3rd step (20 Hz)
- Hydraulic: every 12th step (5 Hz)
- Thermal: step 0 only (1 Hz)

### 4. Pre-allocated Convergence Buffer
Zero-allocation in hot path:
::```cpp
SystemsSystems() {
    static float buf[SIGNAL_COUNT];  // Pre-allocated
    convergence_buffer = buf;
}
```

### 5. AOT-optimized Balance
```cpp
inline void Systems::balance_electrical(void* state, float omega) {
    auto* st = static_cast<SimulationState*>(state);
    float* acc = st->across.data();
    const float* thr = st->through.data();
    const float* inv_g = st->inv_conductance.data();
    const uint32_t count = st->dynamic_signals_count;

    for (uint32_t i = 0; i < count; ++i) {
        if (st->signal_types[i].is_fixed) continue;
        acc[i] += thr[i] * inv_g[i] * omega;
    }
}
```

### Benchmark Results (JIT Mode)
```
Full loop (clear+solve+balance+post): 91.48 ns/step
solve_step only: 38.98 ns/step
SOR balance only: 14.68 ns/step
20 iter convergence: 1118.66 ns/step
```

### Running Benchmarks
```bash
cd /Users/vladimir/an24_cpp/build/examples
./benchmark /Users/vladimir/an24_cpp/src/aircraft/vsu_test.json
```

### Generating AOT Code
```bash
./codegen_test <json_file> <output_dir>
```

## Future Work

1. Add T4 sensor filter (LerpNode) to vsu_dmr_test.json
2. Implement hot-start simulation (weak batteries → thermal stress)
3. Add 3-generator parallel operation (D1, D2, VSU)
4. Add oil pressure simulation
5. Implement drain/false start sequence
