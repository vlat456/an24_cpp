# Component Library

## Overview

The `library/` directory contains JSON type-definition assets for all components. These define:
- Port interface (names, domains, directions)
- Default parameters
- Component metadata

These files are **not** canonical strict-v1 blueprint persistence documents.
Canonical blueprint document authority lives in:

- `knowledge/persistence_spec_v1.md`
- `knowledge/persistence_boundaries.md`

`library/**/*.blueprint` files stay on the library/type-definition path and are consumed by `load_component_registry()`.

## Directory Structure

```
library/
├── electrical/           # Electrical components
│   ├── Battery.blueprint
│   ├── Switch.blueprint
│   ├── Relay.blueprint
│   ├── KnobSwitch.blueprint
│   ├── RotarySwitch1ToN.blueprint
│   ├── RotarySwitchNTo1.blueprint
│   ├── Slider.blueprint
│   ├── Transformer.blueprint
│   ├── CurrentSense.blueprint
│   ├── SolenoidValve.blueprint
│   ├── Voltmeter.blueprint
│   ├── VoltageSense.blueprint
│   ├── ElectricalSource.blueprint
│   ├── ElectricalConductance.blueprint
│   ├── ElectricPump.blueprint
│   └── ElectricHeater.blueprint
├── logical/              # Logic gates and controllers
│   ├── AND.blueprint
│   ├── OR.blueprint
│   ├── NOT.blueprint
│   ├── XOR.blueprint
│   ├── NAND.blueprint
│   ├── Comparator.blueprint
│   ├── LUT.blueprint
│   ├── P.blueprint
│   ├── PD.blueprint
│   ├── PI.blueprint
│   ├── PID.blueprint
│   └── LuaScript.blueprint
├── math/                 # Math operations
│   ├── Add.blueprint
│   ├── Subtract.blueprint
│   ├── Multiply.blueprint
│   ├── Divide.blueprint
│   ├── Clamp.blueprint
│   ├── Min.blueprint
│   ├── Max.blueprint
│   ├── LerpNode.blueprint
│   ├── Normalize.blueprint
│   ├── SlewRate.blueprint
│   ├── AsymSlewRate.blueprint
│   ├── FirstOrderLag.blueprint
│   ├── Integrator.blueprint
│   ├── Accumulator.blueprint
│   ├── TimeDelay.blueprint
│   ├── SampleHold.blueprint
│   ├── Monostable.blueprint
│   ├── Lesser.blueprint
│   ├── LesserEq.blueprint
│   ├── Greater.blueprint
│   └── GreaterEq.blueprint
├── thermal/              # Thermal components
│   └── TempSensor.blueprint
├── mechanical/           # Mechanical components
│   ├── InertiaNode.blueprint
│   └── Spring.blueprint
├── hydraulic/            # Hydraulic components
│   ├── HydraulicPump.blueprint
│   ├── HydraulicValve.blueprint
│   └── HydraulicRef.blueprint
├── pneumatic/            # Pneumatic components
│   ├── PneumaticCompressor.blueprint
│   ├── PneumaticValve.blueprint
│   └── PneumaticRef.blueprint
├── mech/                 # Legacy mechanical
│   └── Spring.blueprint
├── systems/              # Composite blueprints
│   └── 12SAM28.blueprint
├── connectors/           # External I/O connectors
│   └── simconnect/
│       ├── SimConnectInput.blueprint
│       └── SimConnectOutput.blueprint
├── Bus.blueprint         # Bus node
├── RefNode.blueprint     # Reference node
├── Splitter.blueprint    # Signal splitter
├── Merger.blueprint      # Signal merger
├── BlueprintInput.blueprint
├── BlueprintOutput.blueprint
├── Group.blueprint
├── Text.blueprint
├── Value.blueprint
└── library_index.json    # Type registry index
```

## Library Asset Format (v3.0 type-definition path)

### Primitive Component
```json
{
  "version": "3.0",
  "id": "Battery",
  "display_name": "Battery",
  "description": "DC voltage source with internal resistance",
  "cpp_class": true,
  "domains": ["Electrical"],
  "priority": "high",
  "critical": true,
  "interface": [
    {"name": "v_in", "domain": 1, "direction": 0, "type": "V"},
    {"name": "v_out", "domain": 1, "direction": 1, "type": "V"}
  ],
  "param_defaults": {
    "v_nominal": "28.0",
    "internal_r": "0.01"
  }
}
```

### Composite Blueprint
```json
{
  "version": "3.0",
  "id": "simple_battery",
  "display_name": "Simple Battery",
  "cpp_class": false,
  "interface": [
    {"name": "vin", "domain": 1, "direction": 0, "type": "V"},
    {"name": "vout", "domain": 1, "direction": 1, "type": "V"}
  ],
  "nodes": [...],
  "wires": [...]
}
```

## Domain Values

| Domain | Value |
|--------|-------|
| Electrical | 1 |
| Logical | 2 |
| Mechanical | 4 |
| Hydraulic | 8 |
| Thermal | 16 |
| Pneumatic | 32 |

## Port Direction Values

| Direction | Value |
|-----------|-------|
| Input | 0 |
| Output | 1 |
| Bidirectional | 2 |

## Files

| File | Purpose |
|------|---------|
| `library/library_index.json` | Library index (id-to-path mapping) |
| `src/io/json/component_registry_json_loader.h` | JSON loader for registry |
| `src/blueprint_v2/library/library_index.h` | LibraryIndex C++ API |
| `src/core/model/component_registry.h` | ComponentRegistry |
