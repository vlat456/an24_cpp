# Task: Add `static constexpr Domain domain` field to all component classes

## Context
We are implementing multi-domain scheduling for a flight simulator. Each component class needs a `static constexpr Domain domain` field that specifies which physics domains it belongs to (using bitmask for multi-domain components).

## Domain Enum (bitmask)
```cpp
enum class Domain : uint8_t {
    Electrical = 1 << 0,  // 60 Hz
    Logical    = 1 << 1,  // 60 Hz
    Mechanical = 1 << 2,  // 20 Hz
    Hydraulic  = 1 << 3,  // 5 Hz
    Thermal    = 1 << 4   // 1 Hz
};
```

## Requirements

For each component class in `/Users/vladimir/an24_cpp/src/jit_solver/components/all.h`, add the `domain` field IMMEDIATELY after the `public:` declaration.

### Format:
```cpp
template <typename Provider = JitProvider>
class ComponentName {
public:
    static constexpr Domain domain = Domain::Electrical;  // or multi-domain

    Provider provider;
    // ... rest of fields
```

## Component to Domain Mapping

### Electrical (single domain):
- Battery, Switch, Relay, Resistor, Load, RefNode, Bus
- Generator, GS24, RUG82, DMR400, Gyroscope, AGK47
- Transformer, Inverter, LerpNode, Splitter
- IndicatorLight, Voltmeter, HighPowerLoad

### Logical (single domain):
- Comparator, HoldButton

### Mechanical (single domain):
- InertiaNode

### Hydraulic (single domain):
- ElectricPump, SolenoidValve

### Thermal (single domain):
- TempSensor, Radiator

### Multi-domain (bitmask OR):
- **ElectricHeater**: `Domain::Electrical | Domain::Thermal`
- **RU19A**: `Domain::Electrical | Domain::Mechanical | Domain::Thermal`

## Validation
After adding fields, verify:
1. All component classes have the `domain` field
2. Field is placed AFTER `public:` and BEFORE other member variables
3. Multi-domain components use `|` operator to combine domains
4. File compiles without errors: `cmake --build . --target jit_solver`

## Test
After compilation, check log output when running editor:
```
[build] created 15 components (elec=15, mech=1, therm=1)
```

This confirms RU19A is correctly registered in multiple domains.
