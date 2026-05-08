# JIT Solver

Runtime component loading and execution for the editor.

## Overview

```
Blueprint JSON → build_systems_dev() → ComponentVariant map + PushScheduler
                                    → step() each frame
```

## Build Process

### build_systems_dev()
```cpp
BuildResult build_systems_dev(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
);
```

Steps:
1. **Parse devices** — create ComponentVariant for each device from JSON
2. **Setup ports** — populate JitProvider indices from port_to_signal map
3. **Signal allocation** — union-find connected signals
4. **Schedule** — add components to PushScheduler (sources vs consumers)
5. **Extract electrical** — build islands for electrical subsolver
6. **Extract hydraulic** — build islands for hydraulic subsolver (if present)
7. **Extract pneumatic** — build islands for pneumatic subsolver (if present)

### BuildDeviceStore
Guarded component storage. Mutable APIs throw after `seal()`:
```cpp
class BuildDeviceStore {
    using Storage = std::unordered_map<std::string, ComponentVariant>;
    // Mutable APIs: operator[], find_mutable, for_each_mutable
    // Const APIs: at, find, begin, end, size, count
    void seal(); // After seal, mutable APIs throw
};
```

### SolverOwnedRefs
Pre-built typed pointer lists eliminate per-frame `std::visit` scans:
```cpp
struct SolverOwnedRefs {
    std::vector<ControlledVoltageSource<JitProvider>*> controlled_voltage_sources;
    std::vector<VariableConductance<JitProvider>*> variable_conductances;
    std::vector<AZS<JitProvider>*> azs_switches;
    std::vector<HoldButton<JitProvider>*> hold_buttons;
    std::vector<Relay<JitProvider>*> relays;

    std::vector<Battery<JitProvider>*> batteries;
    std::vector<Generator<JitProvider>*> generators;
    std::vector<Resistor<JitProvider>*> resistors;
    std::vector<ElectricalConductance<JitProvider>*> electrical_conductances;
    std::vector<ElectricalSource<JitProvider>*> electrical_sources;
};
```

## PushScheduler

Uses `ScheduledComponent` with type-erased `ErasedStep` dispatch:
```cpp
struct ScheduledComponent {
    void execute(SimulationState& st, double dt) const;
    void commit(SimulationState& st, double dt) const;

    template <typename T>
    static ScheduledComponent for_type(T* component);

private:
    ErasedStep execute_step_;
    ErasedStep commit_step_;
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

**Step order:**
1. Execute all sources
2. Execute all consumers
3. Commit all sources
4. Commit all consumers

## ErasedStep

Type-safe zero-overhead dispatch without virtual functions:
```cpp
struct ErasedStep {
    using InvokeFn = void (*)(void* self, SimulationState& st, double dt);

    void* self = nullptr;
    InvokeFn invoke = nullptr;

    template <typename T>
    static ErasedStep execute_for(T* component);

    template <typename T>
    static ErasedStep commit_for(T* component);
};
```

## Multi-Domain Nodal Solving

The JIT build process creates a `NodalBuildPlan` per domain:
- `electrical_plan` — `Domain::Electrical`
- `hydraulic_plan` — `Domain::Hydraulic`
- `pneumatic_plan` — `Domain::Pneumatic`

Each plan contains islands with elements (FixedNode, Source, Branch). The same `solve_nodal()` function solves all domains.

## Component API

Components implement `execute()` and optionally `commit()`:
```cpp
class MyComponent {
    Provider provider;  // JitProvider or AotProvider

    void execute(SimulationState& st, double dt) {
        float vin = st.values[provider.get(PortNames::v_in)];
        st.values[provider.get(PortNames::v_out)] = vin * gain;
    }

    void commit(SimulationState& st, double dt) {
        // State transitions (one-frame delay)
    }
};
```

## Reading Solved Nodal State

```cpp
void MyComponent::execute(SimulationState& st, double /*dt*/) {
    if (st.electrical_rt != nullptr) {
        float current = get_branch_current(*st.electrical_rt, electrical_handle);
        st.values[provider.get(PortNames::i_out)] = current;
    }
    if (st.hydraulic_rt != nullptr) {
        float flow = get_branch_current(*st.hydraulic_rt, hydraulic_handle);
        st.values[provider.get(PortNames::flow_out)] = flow;
    }
}
```

## Files

| File | Purpose |
|------|---------|
| `src/core/solvers/jit/jit_solver.h` | JIT solver, BuildResult, BuildDeviceStore |
| `src/core/solvers/jit/simulator.h` | Simulator template |
| `src/core/solvers/jit/state.h` | SimulationState |
| `src/core/solvers/jit/scheduler.h` | PushScheduler, ScheduledComponent |
| `src/core/solvers/jit/erased_step.h` | ErasedStep type-erased dispatch |
| `src/core/solvers/jit/subsolvers/nodal_subsolver.h` | Domain-agnostic nodal solver |
| `src/core/solvers/common/nodal_types.h` | NodalBuildPlan, NodalRuntimeState |
| `src/core/solvers/common/build_algorithms.h` | Shared build algorithms |
| `src/core/solvers/common/element_extraction.h` | Shared extraction templates |
