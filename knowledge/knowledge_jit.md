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

## Files

- `src/jit_solver/jit_solver.h` — BuildResult, SolverOwnedRefs
- `src/jit_solver/jit_solver.cpp` — Device creation functions
- `src/jit_solver/scheduler.h` — PushScheduler
- `src/jit_solver/simulator.h` — Simulator<T> template
- `src/jit_solver/state.h` — SimulationState
- `src/jit_solver/components/provider.h` — JitProvider/AotProvider
