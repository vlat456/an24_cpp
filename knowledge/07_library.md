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
│   └── ...
├── logical/              # Logic gates
│   ├── AND.blueprint
│   ├── OR.blueprint
│   ├── PID.blueprint
│   └── ...
├── math/                 # Math operations
│   ├── Add.blueprint
│   ├── Clamp.blueprint
│   ├── SlewRate.blueprint
│   ├── FirstOrderLag.blueprint
│   └── ...
├── thermal/              # Thermal components
│   ├── TempSensor.blueprint
│   └── ...
├── mechanical/           # Mechanical components
│   ├── InertiaNode.blueprint
│   └── ...
├── mech/                 # Legacy mechanical (Spring)
│   └── Spring.blueprint
├── systems/              # Composite blueprints
│   ├── 12SAM28.blueprint
│   └── ...
├── Bus.blueprint         # Special nodes
├── RefNode.blueprint
├── Splitter.blueprint
├── Merger.blueprint
├── BlueprintInput.blueprint
├── BlueprintOutput.blueprint
├── Group.blueprint
├── Text.blueprint
└── Value.blueprint
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
  "nodes": [
    {"id": "bat", "type": "Battery", "params": {"v_nominal": 28.0}},
    {"id": "sw", "type": "Switch"},
    {"id": "vin", "type": "BlueprintInput"},
    {"id": "vout", "type": "BlueprintOutput"}
  ],
  "wires": [
    {"id": "w0", "source": "/vin:port", "target": "/bat:v_in"},
    {"id": "w1", "source": "/bat:v_out", "target": "/sw:v_bus"},
    {"id": "w2", "source": "/sw:v_load", "target": "/vout:port"}
  ]
}
```

## Field Reference

### Root Fields
| Field | Type | Required | Description |
|-------|------|----------|-------------|
| version | string | yes | `"3.0"` for library/type-definition assets |
| id | string | yes | Unique identifier |
| display_name | string | no | Human-readable label for library/type-definition assets |
| description | string | no | Documentation |
| cpp_class | bool | yes | Has C++ implementation? |
| domains | [string] | no | Simulation domains |
| interface | [Port] | yes | Port definitions |
| param_defaults | object | no | Default parameter values |
| priority | string | no | "high", "normal", "low" |
| critical | bool | no | Always include in build |
| nodes | [Node] | composites | Nested nodes |
| wires | [Wire] | composites | Internal connections |

### Port Definition
| Field | Type | Description |
|-------|------|-------------|
| name | string | Port identifier |
| domain | int | 1=Electrical, 2=Logical, 4=Mechanical, 8=Hydraulic, 16=Thermal |
| direction | int | 0=Input, 1=Output, 2=Bidirectional |
| type | string | "V", "I", "Signal", "P", "Q", "T", "H" |

## Boundary Reminder

- `library/**/*.blueprint`: library/type-definition assets
- canonical blueprint documents: strict v1 document format from `knowledge/persistence_spec_v1.md`
- workspace/session files: separate editor-only persistence
- legacy/reference schematics: not canonical authority

### Domain Values
| Value | Domain |
|-------|--------|
| 1 | Electrical |
| 2 | Logical |
| 4 | Mechanical |
| 8 | Hydraulic |
| 16 | Thermal |

### Direction Values
| Value | Direction |
|-------|-----------|
| 0 | Input |
| 1 | Output |
| 2 | Bidirectional |

## Special Node Types

### BlueprintInput / BlueprintOutput
Expose composite ports:
```json
{"id": "vin", "type": "BlueprintInput"}
{"id": "vout", "type": "BlueprintOutput"}
```
Always have a single port named "port".

### Bus
Signal junction - all connected ports share same signal:
```json
{"id": "bus1", "type": "Bus"}
```

### RefNode
Ground reference (0V):
```json
{"id": "gnd", "type": "RefNode"}
```

### Splitter / Merger
Signal routing:
```json
{"id": "split1", "type": "Splitter"}  // 1 input → N outputs
{"id": "merge1", "type": "Merger"}    // N inputs → 1 output
```

### Group / Text
Visual annotation:
```json
{"id": "grp1", "type": "Group", "name": "Power Section"}
{"id": "txt1", "type": "Text", "text": "Note: ..."}
```

## Component Categories

### Electrical Components
| Component | Description |
|-----------|-------------|
| Battery | DC voltage source with internal resistance |
| Generator | RPM-based generator |
| Switch | Binary switch |
| Relay | Electromechanical relay |
| AZS | Automatic circuit breaker (zero-sequence) |
| Resistor | Fixed resistance |
| Voltmeter | Voltage measurement |
| VoltageSense | High-impedance voltage sensing |
| CurrentSense | Current measurement |
| IndicatorLight | Visual indicator |
| ElectricPump | Hydraulic pump |
| ElectricHeater | Thermal load |
| SolenoidValve | Hydraulic valve |
| ElectricalSource | Thevenin voltage source |
| ElectricalConductance | Conductance branch |
| ControlledVoltageSource | Controllable voltage source |
| ControlledCurrentSource | Controllable current source |
| Inverter | Voltage inverter |
| Radiator | Thermal radiator |
| FuelTank | Fuel reserve |
| GidroAccumulator | Hydraulic accumulator |
| Transformer | Isolated transformer |
| VariableConductance | Variable resistance |
| GroundPower | Ground reference node |
| Gyroscope | Gyroscopic sensor |
| HoldButton | Momentary hold button |
| Positive_V_to_Bool | Voltage threshold to boolean |
| Any_V_to_Bool | Any voltage to boolean |
| KnobSwitch | Passive rotary selector (wiper + throws) |
| RotarySwitch1ToN | KnobSwitch alias (1-to-N intent) |
| RotarySwitchNTo1 | KnobSwitch alias (N-to-1 intent) |
| Slider | Linear slide control |

### Logical Components
| Component | Description |
|-----------|-------------|
| AND, OR, NOT, XOR, NAND | Logic gates |
| Comparator | Compare two values |
| Greater, Lesser, GreaterEq, LesserEq | Comparison |
| PID, PI, PD, P | Controllers |
| LUT | Lookup table |

### Math Components
| Component | Description |
|-----------|-------------|
| Add, Subtract, Multiply, Divide | Arithmetic |
| Clamp, Normalize, Min, Max | Range operations |
| SlewRate, AsymSlewRate | Rate limiting |
| FastTMO, AsymTMO | Time constants |
| Integrator | Integration |
| Accumulator | Signal accumulation |
| SampleHold | Sample and hold |
| TimeDelay | Signal delay |
| Monostable | One-shot pulse |
| LerpNode | Linear interpolation |
| FirstOrderLag | First-order lag filter |

### Thermal Components
| Component | Description |
|-----------|-------------|
| TempSensor | Temperature sensor |

### Mechanical Components
| Component | Description |
|-----------|-------------|
| InertiaNode | Rotational inertia |
| Spring | Mechanical spring |
