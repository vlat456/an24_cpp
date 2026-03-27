# Component System

## Component Template Pattern

All components are template classes parameterized by Provider:

```cpp
template <typename Provider = JitProvider>
class Battery {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;  // Port access mechanism
    
    // Parameters (from JSON)
    float v_nominal = 28.0f;
    float internal_r = 0.01f;
    
    // Domain solver
    void solve_electrical(SimulationState& st, float dt);
    
    // Optional lifecycle hooks
    void finalize_step(SimulationState& st, float dt);  // State machine
    void pre_load();  // Initialization
};
```

## Provider Pattern

### JitProvider (Runtime)
```cpp
struct JitProvider {
    std::unordered_map<PortNames, uint32_t> indices;
    
    uint32_t get(PortNames p) const;  // Runtime hash lookup
    bool has(PortNames p) const;
    void set(PortNames p, uint32_t idx);
};
```

### AotProvider (Compile-Time)
```cpp
template <PortNames P, uint32_t Idx>
struct Binding { static constexpr PortNames key = P; static constexpr uint32_t value = Idx; };

template <typename... Bindings>
struct AotProvider {
    static constexpr uint32_t get(PortNames p);  // Compile-time constant!
};

// Usage:
using Provider = AotProvider<
    Binding<PortNames::v_in, 0>,
    Binding<PortNames::v_out, 1>
>;
```

## Port Access Pattern

```cpp
// Read port value
float v_bus = st.across[provider.get(PortNames::v_bus)];

// Write port value
st.across[provider.get(PortNames::rpm_out)] = rpm_value;

// Stamp conductance
stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
    provider.get(PortNames::v_in), provider.get(PortNames::v_out), g);
```

## ComponentVariant (Type-Safe Dispatch)

All JIT components combined into a variant:
```cpp
using ComponentVariant = std::variant<
    Battery<JitProvider>,
    Switch<JitProvider>,
    AND<JitProvider>,
    OR<JitProvider>,
    // ... 80+ components
>;
```

Operations use `std::visit`:
```cpp
std::visit([&](auto& comp) {
    comp.solve_electrical(st, dt);
}, variant);
```

## PORTS Macro

Generates port index fields for legacy components:
```cpp
class RU19A {
public:
    PORTS(RU19A, v_bus, v_start, k_mod, rpm_out, t4_out)
    // Expands to:
    // uint32_t v_bus_idx = 0;
    // uint32_t v_start_idx = 0;
    // ...
};
```

## Component Categories

### Electrical (`Domain::Electrical`)
- Sources: `Battery`, `Generator`
- Switches: `Switch`, `Relay`, `AZS`
- Loads: `Load`, `Resistor`, `HighPowerLoad`
- Sensors: `Voltmeter`, `CurrentSense`
- Displays: `IndicatorLight`
- Hydraulic: `ElectricPump`, `SolenoidValve`, `GidroAccumulator`

### Logical (`Domain::Logical`)
- Gates: `AND`, `OR`, `NOT`, `XOR`, `NAND`
- Comparison: `Comparator`, `Greater`, `Lesser`, `GreaterEq`, `LesserEq`
- Controllers: `PID`, `PI`, `PD`, `P`
- Lookup: `LUT`

### Math (`Domain::Logical`)
- Arithmetic: `Add`, `Subtract`, `Multiply`, `Divide`
- Clamping: `Clamp`, `Normalize`, `Min`, `Max`
- Filters: `FastTMO`, `AsymTMO`, `SlewRate`, `AsymSlewRate`
- Control: `Integrator`, `LerpNode`, `SampleHold`, `TimeDelay`, `Monostable`

### Mechanical (`Domain::Mechanical`)
- `InertiaNode`, `Spring`

### Thermal (`Domain::Thermal`)
- `TempSensor`, `ElectricHeater`, `Radiator`

### Special
- `Bus` - Signal junction
- `RefNode` - Ground reference
- `Splitter` / `Merger` - Signal routing
- `BlueprintInput` / `BlueprintOutput` - Composite boundaries
- `Group` / `Text` - Visual annotation

## Adding a New Component

1. Create header in `src/jit_solver/components/`:
```cpp
#pragma once
#include "../provider.h"
#include "../state.h"

template <typename Provider = JitProvider>
class MyComponent {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;
    float my_param = 1.0f;
    
    void solve_electrical(SimulationState& st, float dt) {
        float in = st.across[provider.get(PortNames::v_in)];
        st.across[provider.get(PortNames::v_out)] = in * my_param;
    }
};
```

2. Add to `ComponentVariant` in `jit_solver.h`

3. Add to `all.h` include list

4. Create library definition in `library/electrical/MyComponent.blueprint`

5. Run `tools/update_port_registry.cpp` to regenerate `port_registry.h`
