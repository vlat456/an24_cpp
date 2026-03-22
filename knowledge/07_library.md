# Component Library

## Overview

The `library/` directory contains JSON blueprint definitions for all components. These define:
- Port interface (names, domains, directions)
- Default parameters
- Component metadata

## Directory Structure

```
library/
├── electrical/           # Electrical components
│   ├── Battery.blueprint
│   ├── Switch.blueprint
│   ├── Relay.blueprint
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
│   └── ...
├── thermal/              # Thermal components
│   ├── TempSensor.blueprint
│   └── ...
├── mechanical/           # Mechanical components
│   ├── InertiaNode.blueprint
│   └── ...
├── systems/              # Composite blueprints
│   ├── RU19A.blueprint
│   ├── GS24.blueprint
│   └── ...
├── Bus.blueprint         # Special nodes
├── RefNode.blueprint
├── Splitter.blueprint
└── ...
```

## Blueprint Format (v3.0)

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
| version | string | yes | "3.0" |
| id | string | yes | Unique identifier |
| display_name | string | no | Human-readable name |
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
| Battery | DC voltage source |
| Generator | RPM-based generator |
| Switch | Binary switch |
| Relay | Electromechanical relay |
| AZS | Automatic circuit breaker |
| Resistor | Fixed resistance |
| Load | Variable load |
| Voltmeter | Voltage measurement |
| CurrentSense | Current measurement |
| IndicatorLight | Visual indicator |
| ElectricPump | Hydraulic pump |
| SolenoidValve | Hydraulic valve |

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
| SampleHold | Sample and hold |
| TimeDelay | Signal delay |
| Monostable | One-shot pulse |
