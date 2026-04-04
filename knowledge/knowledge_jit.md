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
5. **Extract electrical** — build islands for subsolver

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

Type-erased function pointers for zero-overhead dispatch:
```cpp
struct ComponentEntry {
    using ExecuteFn = void (*)(void* self, SimulationState& st, double dt);
    using CommitFn = void (*)(void* self, SimulationState& st, double dt);
    
    void* self = nullptr;
    ExecuteFn execute = nullptr;
    CommitFn commit = nullptr;
};

class PushScheduler {
    std::vector<ComponentEntry> sources_;
    std::vector<ComponentEntry> consumers_;
    
public:
    template <typename T>
    void add_source(T* component);
    
    template <typename T>
    void add_consumer(T* component);
    
    void step(SimulationState& st, double dt);
};
```

## Component API

Components implement execute() and optionally commit():
```cpp
class MyComponent {
    Provider provider;  // JitProvider or AotProvider
    
    void execute(SimulationState& st, double dt) {
        float vin = st.values[provider.get(PortNames::v_in)];
        st.values[provider.get(PortNames::v_out)] = vin * gain;
    }
    
    void commit(SimulationState& st, double dt) {
        // Called after push scheduler
        // Use for state transitions, battery discharge, etc.
    }
};
```

## Port Access (JIT)

```cpp
// Runtime lookup via JitProvider
float v = st.values[provider.get(PortNames::v_in)];
st.values[provider.get(PortNames::v_out)] = output;
```

## Component Creation

Each component type has a creation function in jit_solver.cpp:
```cpp
else if (dev.classname == "Comparator") {
    Comparator<JitProvider> comp;
    comp.Von = param_reader.consume_float_optional("Von", 5.0f);
    comp.Voff = param_reader.consume_float_optional("Voff", 2.0f);
    setup_ports(comp);
    result.devices[dev.name] = comp;
    result.scheduler.add_consumer(&std::get<Comparator<JitProvider>>(result.devices[dev.name]));
}
```

## Signal Overrides

Documents provide interactive control overrides:
```cpp
void Simulator::apply_overrides(const std::unordered_map<std::string, float>& overrides) {
    for (const auto& [signal_name, value] : overrides) {
        if (auto it = port_to_signal.find(signal_name); it != port_to_signal.end()) {
            state.values[it->second] = value;
        }
    }
}
```

## Build Phases (Modular Files)

The build process is split into modular files, each ≤500 LOC:

| File | LOC | Purpose |
|------|-----|---------|
| `jit_solver.cpp` | 34 | Main orchestrator - calls 4 build phases |
| `jit_solver_internal.h` | 70 | Internal API in `jit_solver_impl` namespace |
| `build_utils.cpp` | 120 | Helper functions + ParamReader |
| `build_signals.cpp` | 140 | Union-find + signal allocation |
| `build_components.cpp` | 973 | Component factory (68+ types) |
| `build_electrical.cpp` | 517 | Electrical islands + handle assignment |

Pipeline:
1. **Utils** — metadata helpers, ParamReader
2. **Signals** — union-find, port unions
3. **Components** — factory, scheduler, refs
4. **Electrical** — islands, handle assignment

## Namespace Pattern

Internal implementation uses `jit_solver_impl` named namespace instead of anonymous namespace. This is required because:
- Functions declared in `jit_solver_internal.h` are defined in separate `.cpp` files
- Anonymous namespace gives internal linkage → linker errors when declarations and definitions are split across TUs
- Named namespace with header declarations + cpp definitions works correctly

## Files

- `src/core/solvers/jit/jit_solver.cpp` — Main orchestrator
- `src/core/solvers/jit/jit_solver_internal.h` — Internal API
- `src/core/solvers/jit/build_utils.cpp` — Helpers + ParamReader
- `src/core/solvers/jit/build_signals.cpp` — Signal allocation
- `src/core/solvers/jit/build_components.cpp` — Component factory
- `src/core/solvers/jit/build_electrical.cpp` — Electrical islands
- `src/core/solvers/jit/jit_solver.h` — BuildResult, SolverOwnedRefs
- `src/core/solvers/jit/scheduler.h` — PushScheduler
- `src/core/solvers/jit/simulator.h` — Simulator<T> template
- `src/core/solvers/jit/state.h` — SimulationState
- `src/core/solvers/jit/components/provider.h` — JitProvider/AotProvider
