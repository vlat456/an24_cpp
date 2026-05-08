# Simulation Engine

The simulation system uses a **hybrid model** combining push scheduling with domain-specific nodal subsolvers.

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
│  │ ├── hydraulic_plan (islands)                            │   │
│  │ ├── pneumatic_plan (islands)                            │   │
│  │ └── solver_owned (typed pointer lists)                  │   │
│  └─────────────────────────────────────────────────────────┘   │
│                              ↓                                   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ SimulationState                                         │   │
│  │ ├── values[] (all runtime signals)                      │   │
│  │ ├── lut_keys[], lut_values[]                            │   │
│  │ ├── electrical_rt (valid during step, electrical solve) │   │
│  │ ├── hydraulic_rt (valid during step, hydraulic solve)   │   │
│  │ └── pneumatic_rt (valid during step, pneumatic solve)   │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Step Pipeline

`Simulator::step(dt)` sequence:

1. **Clamp dt** — `dt = std::min(dt, MAX_DT)` (MAX_DT=0.1s) prevents physics explosions
2. Set domain runtime state pointers on `state` (electrical_rt, hydraulic_rt, pneumatic_rt)
3. **Pre-solve** — update dynamic sources (CVS, variable conductance, AZS)
4. **Solve electrical** — `solve_nodal(electrical_plan, ...)`
5. **Solve hydraulic** — `solve_nodal(hydraulic_plan, ...)` (if present)
6. **Solve pneumatic** — `solve_nodal(pneumatic_plan, ...)` (if present)
7. **Solver-owned execute ops** — `run_solver_owned_ops(solver_execute_ops, state, dt)`
8. **Push scheduler** — `scheduler.step(state, dt)` runs all logical/mechanical/etc components
9. **Solver-owned commit ops** — `run_solver_owned_ops(solver_commit_ops, state, dt)`
10. Clear domain runtime state pointers
11. Advance `time_ += dt`, `step_count_++`

## Key Classes

### Simulator
```cpp
template <typename SolverTag>
class Simulator {
    static constexpr double MAX_DT = 0.1;

    std::optional<BuildResult> build_result_;
    SimulationState state_;
    bool running_ = false;
    double time_ = 0.0;
    uint64_t step_count_ = 0;

public:
    void start(const JitBuildInput& input);
    void stop();
    void step(double dt);

    bool is_running() const { return running_; }
    bool is_built() const { return build_result_.has_value(); }

    double get_time() const { return time_; }
    uint64_t get_step_count() const { return step_count_; }
    size_t get_signal_count() const { return state_.values.size(); }

    float* values() { return state_.values.data(); }
    const float* values() const { return state_.values.data(); }

    float get_signal_value(core::InternedId key) const;
    void apply_typed_overrides(const std::vector<std::pair<core::InternedId, float>>& overrides);
    const core::StringInterner& signal_key_interner() const;
    core::InternedId resolve_signal_key(std::string_view node_id, std::string_view port_name) const;
};
```

### SimulationState
```cpp
struct SimulationState {
    std::vector<float> values;
    std::vector<float> lut_keys;
    std::vector<float> lut_values;

    NodalRuntimeState* electrical_rt = nullptr;
    NodalRuntimeState* hydraulic_rt = nullptr;
    NodalRuntimeState* pneumatic_rt = nullptr;

    [[nodiscard]] uint32_t allocate_signal(float initial_value);
    float signal(uint32_t idx) const;
    float& signal(uint32_t idx);
};
```

### PushScheduler
Uses `ScheduledComponent` with type-erased `ErasedStep` dispatch:
```cpp
struct ScheduledComponent {
    void execute(SimulationState& st, double dt) const;
    void commit(SimulationState& st, double dt) const;

    template <typename T>
    static ScheduledComponent for_type(T* component);
};

class PushScheduler {
    std::vector<ScheduledComponent> sources_;
    std::vector<ScheduledComponent> consumers_;

public:
    template <typename T> void add_source(T* component);
    template <typename T> void add_consumer(T* component);
    void step(SimulationState& st, double dt);
};
```

## Nodal Subsolver (Domain-Agnostic)

The same `solve_nodal` function handles electrical, hydraulic, and pneumatic networks:

```cpp
void solve_nodal(
    const NodalBuildPlan& plan,
    const std::vector<float>& element_value_a,
    SimulationState& st,
    NodalRuntimeState& rt,
    double dt
) noexcept;
```

Each domain has its own `NodalBuildPlan` and `NodalRuntimeState`, but shares the same solver implementation. The domain is determined by which runtime pointer is active (electrical_rt, hydraulic_rt, pneumatic_rt).

## Build Process

`Simulator::start()` internally calls `build_systems_dev()`:

1. Parse devices from `JitBuildInput`
2. Setup port indices in `JitProvider`
3. Allocate signals via union-find
4. Build domain-specific nodal plans (electrical, hydraulic, pneumatic)
5. Populate `PushScheduler` with sources and consumers
6. Seal `BuildDeviceStore` to prevent post-build mutation

## File Locations

| File | Path |
|------|------|
| Simulator | `src/core/solvers/jit/simulator.h` |
| SimulationState | `src/core/solvers/jit/state.h` |
| PushScheduler | `src/core/solvers/jit/scheduler.h` |
| ErasedStep | `src/core/solvers/jit/erased_step.h` |
| Nodal Subsolver | `src/core/solvers/jit/subsolvers/nodal_subsolver.h` |
| Nodal Types | `src/core/solvers/common/nodal_types.h` |
| Build Algorithms | `src/core/solvers/common/build_algorithms.h` |
| Domain Types | `src/core/domain_types.h` |
