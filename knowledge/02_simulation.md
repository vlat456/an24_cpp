# Simulation Engine

The simulation system uses a **hybrid model** combining push scheduling with electrical subsolver.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Simulator<T>                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ BuildResult                                              │   │
│  │ ├── signal_count, fixed_signals, port_to_signal         │   │
│  │ ├── devices (ComponentVariant map)                      │   │
│  │ ├── scheduler (PushScheduler)                            │   │
│  │ ├── electrical_plan (islands)                           │   │
│  │ └── solver_owned (typed pointer lists)                  │   │
│  └─────────────────────────────────────────────────────────┘   │
│                              ↓                                   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ SimulationState                                         │   │
│  │ ├── values[] (all signals)                              │   │
│  │ ├── signal_types[] (domain, is_fixed)                   │   │
│  │ ├── lut_keys[], lut_values[]                            │   │
│  │ └── electrical_rt (valid during step)                   │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Step Pipeline

`Simulator::step(dt)` sequence:

1. **Clamp dt** — `dt = std::min(dt, MAX_DT)` (MAX_DT=0.1s) prevents physics explosions
2. Set `state.electrical_rt = &electrical_rt_` (RAII guard)
3. **Pre-solve** — `update_dynamic_sources()` stamps actuator states from previous frame
4. **Solve electrical** — `solve_electrical(electrical_plan, state, electrical_rt_, dt)`
5. **Push scheduler** — `scheduler.step(state, dt)` runs all logical/mechanical/etc components
6. **Commit pass** — `commit_solver_owned_devices()` handles battery discharge, state transitions
7. Clear `state.electrical_rt`
8. Advance `time_ += dt`, `step_count_++`

## Key Classes

### Simulator
```cpp
template <typename SolverTag>
class Simulator {
    static constexpr double MAX_DT = 0.1;

    std::optional<BuildResult> build_result_;
    SimulationState state_;
    ElectricalRuntimeState electrical_rt_;
    bool running_ = false;
    double time_ = 0.0;
    uint64_t step_count_ = 0;

public:
    void start_from_json(const std::string& json_str);
    void step(double dt);
    void apply_overrides(const std::unordered_map<std::string, float>& overrides);

    float get_wire_voltage(const std::string& port_name) const;
    bool get_boolean_output(const std::string& port_name) const;
    double get_battery_charge(const std::string& device_name) const;
};
```

### BuildResult
```cpp
struct BuildResult {
    uint32_t signal_count;
    std::vector<uint32_t> fixed_signals;
    PortToSignal port_to_signal;

    std::unordered_map<std::string, ComponentVariant> devices;
    PushScheduler scheduler;
    ElectricalBuildPlan electrical_plan;
    SolverOwnedRefs solver_owned;
    std::vector<float> lut_keys;
    std::vector<float> lut_values;
};
```

### SimulationState
```cpp
struct SimulationState {
    std::vector<float> values;
    std::vector<SignalType> signal_types;
    std::vector<float> lut_keys;
    std::vector<float> lut_values;
    uint32_t dynamic_signals_count = 0;
    ElectricalRuntimeState* electrical_rt = nullptr;  // Valid only during step()
};
```

### ComponentVariant
Type-safe polymorphic storage using `std::variant`:
```cpp
using ComponentVariant = std::variant<
    Battery<JitProvider>,
    Switch<JitProvider>,
    AND<JitProvider>,
    OR<JitProvider>,
    Comparator<JitProvider>,
    // ... all components (~70 types)
>;
```

## Domain Scheduling

| Domain | Frequency | Execution |
|--------|-----------|-----------|
| Electrical | 60 Hz | Local island subsolver |
| Logical | 60 Hz | Push scheduler |
| Mechanical | 20 Hz | Push scheduler |
| Hydraulic | 5 Hz | Push scheduler |
| Thermal | 1 Hz | Push scheduler |

## See Also

- `03_components.md` — Component API (execute/commit)
- `10_quick_reference.md` — Build commands and file locations
