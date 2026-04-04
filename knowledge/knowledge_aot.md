# AOT Code Generation

Generates optimized C++ source files from blueprints for maximum runtime performance.

## Overview

```
Blueprint JSON → expand_sub_blueprint_refs → signal allocation → generate C++
                                              (union-find)
```

## Key Classes

### CodeGen
```cpp
class CodeGen {
public:
    // Generate header/source for flat device list
    static std::string generate_header(
        const std::string& source_file,
        const std::vector<DeviceInstance>& devices,
        const std::vector<Connection>& connections,
        const std::unordered_map<std::string, uint32_t>& port_to_signal,
        uint32_t signal_count,
        const std::string& class_name = "Systems",
        const ElectricalPlanCodegen& electrical_plan = {}
    );

    static std::string generate_source(...);

    // Generate for composite blueprints (hierarchical)
    static CompositeCodegenResult generate_composite_systems(
        const TypeDefinition& td,
        const TypeRegistry& registry);

    static std::map<std::string, CompositeCodegenResult> generate_all_composites(
        const TypeRegistry& registry);

    static void generate_port_registry(const TypeRegistry& registry, const std::string& output_path);
};
```

### CompositeCodegenResult
```cpp
struct CompositeCodegenResult {
    std::string header;
    std::string source;
    std::string class_name;
};
```

## Signal Allocation

Union-find algorithm to group connected ports into single signals:

```cpp
// Example: battery.v_out → load.v_in → ref_node.gnd
// These ports share one signal index
signal 0: battery.v_out, load.v_in, ref_node.gnd
signal 1: battery.gnd, load.gnd
```

## Generated Code Structure

### Header
```cpp
struct Systems {
    // Component fields (no heap allocation)
    Battery aot_battery;
    Resistor aot_load;
    Switch aot_switch;
    
    // Signal arrays (SoA layout)
    float values[16];
    SignalType signal_types[16];
    
    // Pre-built electrical islands
    static constexpr ElectricalIsland islands[2] = {...};
    
    Systems();
    void step(double dt);
    void pre_load();
};
```

### Source
```cpp
void Systems::step(double dt) {
    // 1. Pre-solve (dynamic sources)
    aot_azs.execute(*this, dt);
    
    // 2. Solve electrical islands
    solve_island_0(values, islands[0], dt);
    
    // 3. Push scheduler (logical, mechanical, etc.)
    aot_comparator.execute(*this, dt);
    aot_and_gate.execute(*this, dt);
    
    // 4. Commit pass
    aot_battery.commit(*this, dt);
}
```

## Provider Pattern (AOT vs JIT)

### AotProvider
Compile-time constexpr port lookup:
```cpp
template <typename... Bindings>
struct AotProvider {
    static constexpr uint32_t get(PortNames p) {
        uint32_t result = UINT32_MAX;
        ((p == Bindings::key ? (result = Bindings::value, void()) : void()), ...);
        return result;
    }
};

// Usage: state.values[aot.get(PortNames::v_in)]
// Compiles to: state.values[3] (direct constant)
```

### JitProvider
Runtime flat-array lookup:
```cpp
struct JitProvider {
    uint32_t indices[static_cast<size_t>(PortNames::_COUNT)];
    
    uint32_t get(PortNames p) const {
        return indices[static_cast<uint32_t>(p)];
    }
};
```

## Electrical Plan Codegen

Mirror of runtime electrical plan for static code generation:

```cpp
struct ElectricalIslandPlanCodegen {
    std::vector<uint32_t> signal_indices;
    std::vector<ElectricalElementCodegen> elements;  // FixedVoltageNode, TheveninSource, ConductanceBranch
};

struct ElectricalPlanCodegen {
    std::vector<ElectricalIslandPlanCodegen> islands;
    std::vector<DeviceBinding> device_bindings;
};
```

## Composite Codegen

Hierarchical codegen for nested blueprints:

1. **Expand** — `expand_sub_blueprint_references()` flattens nested blueprints
2. **Merge** — merge device instances with type definitions (ports, params, domains)
3. **Allocate** — union-find signal allocation
4. **Generate** — emit C++ header/source

### BlueprintInput/Output Bridge Union

Critical: Must match JIT solver's wire unification logic:
```cpp
// Both paths must unify ".ext" and ".port" identically
// JIT: jit_solver.cpp bridge unification
// AOT: here, for composite blueprints
```

## Build Targets

```bash
# Generate port registry from library blueprints
cmake --build build --target update_port_registry

# Generate all composite systems
cmake --build build --target codegen_composites

# Full build (includes codegen)
cmake --build build
```

## Files

- `src/codegen/codegen.h` — API declarations
- `src/codegen/codegen.cpp` — Implementation
- `src/jit_solver/components/port_registry.h` — Auto-generated from library
