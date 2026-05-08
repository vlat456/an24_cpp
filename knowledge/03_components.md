# Component System

This document reflects the current component model after electrical subsolver migration and multi-domain expansion.

## Component Shape

Components are template classes parameterized by provider:

```cpp
template <typename Provider = JitProvider>
class MyComponent {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
```

Important:

- runtime hook names are `execute` and `commit` (not `solve_*` / `finalize_step`)
- `commit` now always receives `dt`

---

## Provider Pattern

### JitProvider (runtime)

```cpp
struct JitProvider {
    uint32_t get(PortNames p) const;
    bool has(PortNames p) const;
    void set(PortNames p, uint32_t idx);
};
```

### AotProvider (compile-time)

```cpp
template <PortNames P, uint32_t Idx>
struct Binding { ... };

template <typename... Bindings>
struct AotProvider {
    static constexpr uint32_t get(PortNames p);
};
```

---

## ComponentVariant

All JIT components are stored in a `std::variant` (`ComponentVariant`) and dispatched via `std::visit` or pre-built typed pointers.

Files:

- `src/core/solvers/jit/components/all.h`
- `src/core/solvers/jit/jit_solver.h`

---

## Domains

```cpp
enum class Domain : uint8_t {
    Electrical = 1 << 0,   // 1
    Logical    = 1 << 1,   // 2
    Mechanical = 1 << 2,   // 4
    Hydraulic  = 1 << 3,   // 8
    Thermal    = 1 << 4,   // 16
    Pneumatic  = 1 << 5,   // 32
};
```

Domains are bitmasks — a component can belong to multiple domains.

## Port Types

```cpp
enum class PortType {
    V,              // Voltage (Electrical default)
    I,              // Current (Electrical default)
    Signal,         // Generic float signal (Logical default)
    Bool,           // Boolean signal (Logical default)
    RPM,            // Rotational speed (Mechanical default)
    Temperature,    // Thermal
    Pressure,       // Pressure (Hydraulic default)
    Position,       // Linear/angular position (Mechanical default)
    Contextual,     // Domain-inferred from context
    Any,            // Wildcard / untyped
};
```

---

## Electrical Component Categories (Current)

### Solver-owned propagators

These provide electrical elements to the subsolver; they do not push-write electrical propagation:

- wrappers: `Battery`, `Generator`, `Resistor`
- primitives: `ElectricalSource`, `ElectricalConductance`

### Fixed reference

- `RefNode` clamps node potential (also represented as solver fixed node)

### Observers/derived outputs

- `IndicatorLight`: brightness from solved `v_in`
- `CurrentSense`: `i_out` from solved branch current handle

### Widget-interactive components

These components expose interactive widgets (knobs, sliders, toggles) in the visual editor:

- `KnobSwitch`: Multi-position rotary switch with visual tick marks
- `Slider`: Linear slide control
- `HoldButton`: Momentary pushbutton
- `Switch`: Binary toggle switch
- `IndicatorLight`: Visual output indicator

### KnobSwitch semantics

- `KnobSwitch` is a passive rotary contact with one wiper (`wiper`) and multiple throws (`throw1..throwN`).
- Electrically, `wiper -> throwN` and `throwN -> wiper` are both valid; this is the same topology.
- `InOut` on `wiper`/`throw*` is used to model passive contacts, not directed dataflow.

---

## Component Registry

`ComponentRegistry` (formerly `TypeRegistry`) is the authoritative registry of all component types:

```cpp
struct ComponentRegistry {
    void register_type(const std::string& classname, ComponentSpec spec,
                       TypePresentation pres = {}, std::string category = "");
    const ComponentSpec* get(const std::string& classname) const;
    bool has(const std::string& classname) const;
    std::vector<std::string> list_classnames() const;
    const std::unordered_map<std::string, ComponentSpec>& all_types() const;
};
```

File: `src/core/model/component_registry.h`

---

## Library Component Inventory

### Electrical
`Battery`, `Generator`, `Resistor`, `Switch`, `Relay`, `KnobSwitch`, `RotarySwitch1ToN`, `RotarySwitchNTo1`, `Slider`, `HoldButton`, `IndicatorLight`, `Transformer`, `CurrentSense`, `SolenoidValve`, `Voltmeter`, `VoltageSense`, `ElectricalSource`, `ElectricalConductance`, `ElectricPump`, `ElectricHeater`

### Logical
`AND`, `OR`, `NOT`, `XOR`, `NAND`, `Comparator`, `LUT`, `P`, `PD`, `PI`, `PID`

### Math
`Add`, `Subtract`, `Multiply`, `Divide`, `Clamp`, `Min`, `Max`, `LerpNode`, `Normalize`, `SlewRate`, `AsymSlewRate`, `FirstOrderLag`, `Integrator`, `Accumulator`, `TimeDelay`, `SampleHold`, `Monostable`, `Lesser`, `LesserEq`, `Greater`, `GreaterEq`

### Mechanical
`InertiaNode`, `Spring`

### Hydraulic
`HydraulicPump`, `HydraulicValve`, `HydraulicRef`

### Pneumatic
`PneumaticCompressor`, `PneumaticValve`, `PneumaticRef`

### Thermal
`TempSensor`

### Special / Structural
`RefNode`, `Bus`, `Splitter`, `Merger`, `BlueprintInput`, `BlueprintOutput`, `Group`, `Text`, `Value`

### Connectors
`SimConnectInput`, `SimConnectOutput`

### Systems (Composite)
`12SAM28`

### Logical / Scripting
`LuaScript`

---

## Primitive-First Direction

Two first-class electrical primitives exist:

1. `ElectricalSource` — solver-owned voltage source
2. `ElectricalConductance` — solver-owned conductance branch

Wrappers (`Battery`, `Resistor`) still exist but new components should prefer primitives with `solver_role` metadata.

---

## Files

| File | Purpose |
|------|---------|
| `src/core/solvers/jit/components/all.h` | All component includes |
| `src/core/solvers/common/provider.h` | Provider pattern |
| `src/core/solvers/jit/state.h` | SimulationState |
| `src/core/model/component_registry.h` | ComponentRegistry |
| `src/core/model/component_spec.h` | ComponentSpec |
| `src/core/domain_types.h` | Domain, PortType enums |
| `src/core/solvers/common/port_registry.h` | Port name registry |
| `src/core/solvers/jit/build_factory.cpp` | AUTO-GENERATED component factory |
| `src/core/solvers/aot/codegen_registry.cpp` | Codegen tool that produces the factory |
