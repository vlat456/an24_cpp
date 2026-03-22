# AOT Code Generation

## Overview

The code generator produces optimized C++ source files from blueprint definitions. AOT (Ahead-Of-Time) mode enables:
- Compile-time port index resolution
- No virtual dispatch overhead
- Full compiler optimization (inlining, constant folding)

## CodeGen Class

```cpp
class CodeGen {
public:
    // Generate single composite
    static CompositeCodegenResult generate_composite_systems(
        const TypeDefinition& td,
        const TypeRegistry& registry);
    
    // Generate all composites in topological order
    static std::map<std::string, CompositeCodegenResult> generate_all_composites(
        const TypeRegistry& registry);
    
    // Generate port registry header
    static void generate_port_registry(
        const TypeRegistry& registry, 
        const std::string& output_path);
};
```

## Generated Code Structure

### Header (.h)
```cpp
#pragma once
#include "jit_solver/state.h"
#include "jit_solver/components/provider.h"

// AOT Provider with compile-time port bindings
using MyComposite_Provider = AotProvider<
    Binding<PortNames::v_in, 0>,
    Binding<PortNames::v_out, 1>,
    // ...
>;

class MyComposite {
public:
    static constexpr Domain domain = Domain::Electrical;
    MyComposite_Provider provider;
    
    // Component instances
    Battery<MyComposite_Provider> battery;
    Switch<MyComposite_Provider> sw;
    
    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
};
```

### Source (.cpp)
```cpp
#include "generated_MyComposite.h"

void MyComposite::solve_electrical(SimulationState& st, float dt) {
    // Direct calls - no virtual dispatch
    battery.solve_electrical(st, dt);
    sw.solve_electrical(st, dt);
}

void MyComposite::post_step(SimulationState& st, float dt) {
    battery.post_step(st, dt);
    sw.post_step(st, dt);
}
```

## Generation Pipeline

```
Blueprint JSON
      ↓
TypeRegistry (load all types)
      ↓
Topological Sort (dependencies first)
      ↓
For each composite:
  ├── Flatten nested blueprints
  ├── Union-Find signal allocation
  ├── Generate AotProvider bindings
  └── Emit C++ code
      ↓
Compile generated code
      ↓
Link with application
```

## JIT vs AOT Comparison

| Aspect | JIT | AOT |
|--------|-----|-----|
| Port access | Runtime hash lookup | Compile-time constant |
| Dispatch | std::visit (variant) | Direct function call |
| Flexibility | Load any blueprint | Fixed at compile time |
| Performance | Good | Maximum |
| Use case | Editor | Production/release |

## Port Registry Generation

`tools/update_port_registry.cpp` generates `port_registry.h`:

```cpp
// Auto-generated - do not edit
#pragma once

enum class PortNames : uint32_t {
    // Battery
    v_in = 0,
    v_out = 1,
    
    // Switch
    v_bus = 2,
    v_load = 3,
    control = 4,
    
    // ... all ports from all components
};

constexpr const char* port_name_to_string(PortNames p);
PortNames string_to_port_name(const char* s);
```

## Generated Files Location

```
generated/
├── generated_GSC.h          # Composite header
├── generated_GSC.cpp        # Composite source
├── generated_vsu_test.h     # Test composite
├── bench_vsu_aot.cpp        # Benchmark
└── ...
```

## Usage in Code

### Running AOT Simulation
```cpp
#include "generated/generated_MySystem.h"

int main() {
    SimulationState st;
    st.across.resize(10, 0.0f);
    // ... initialize state
    
    MySystem system;
    // ... configure provider bindings
    
    for (int i = 0; i < 1000; ++i) {
        st.clear_through();
        system.solve_electrical(st, 1.0f/60.0f);
        st.precompute_inv_conductance();
        solve_sor_iteration(st.across.data(), st.through.data(), 
                           st.inv_conductance.data(), st.dynamic_signals_count, 1.3f);
        system.post_step(st, 1.0f/60.0f);
    }
}
```

## Signal Allocation

Union-Find algorithm assigns signal indices:

1. Each port starts in its own set
2. Wires merge connected ports
3. Each set gets one signal index
4. Fixed signals (boundary conditions) go to end of array

```cpp
// Result: port_to_signal map
{
    "/battery:v_out": 0,
    "/load:v_in": 0,      // Same signal - connected
    "/switch:control": 1,
    // ...
}
```
