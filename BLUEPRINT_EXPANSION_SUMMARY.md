# Nested Blueprint Expansion Analysis

## Overview
Nested/composite blueprints in the An-24 Flight Simulation are expanded during JSON parsing using a recursive process that creates "bridge nodes" (BlueprintInput/BlueprintOutput components) to handle the interface between parent and expanded nested blueprints.

---

## 1. EXPANSION LOGIC FOR NESTED BLUEPRINTS

### File: `src/json_parser/json_parser.cpp`
**Function**: `parse_json_impl()` (lines 419-681)

**Key Logic** (lines 470-508):
- **Line 471**: Check if component is blueprint type (`!def->cpp_class && !def->devices.empty()`)
- **Line 472-475**: Cycle detection - prevents circular blueprint references
- **Line 478-479**: Log expansion event
- **Line 483-497**: Build nested JSON from TypeDefinition's devices and connections
- **Line 500**: Add classname to expanding set for cycle detection
- **Line 501**: Recursively call `parse_json_impl()` to process nested blueprint
- **Line 502**: Call `merge_nested_blueprint()` to merge expanded devices into parent context

#### Cycle Detection Mechanism:
```cpp
// Line 473-475
if (expanding.count(raw_dev.classname)) {
    throw std::runtime_error("Blueprint cycle detected: '" + raw_dev.classname +
        "' is already being expanded (circular dependency)");
}
```

#### Recursive Expansion:
```cpp
// Lines 500-502
expanding.insert(raw_dev.classname);
ParserContext nested = parse_json_impl(nested_json.dump(), registry, expanding);
merge_nested_blueprint(ctx, nested, raw_dev.name);
```

---

## 2. BLUEPRINT INPUT/OUTPUT BRIDGE NODE CREATION

### File: `src/json_parser/json_parser.cpp`
**Function**: `merge_nested_blueprint()` (lines 353-375)

**Purpose**: Merges expanded nested blueprint devices into parent context

```cpp
static void merge_nested_blueprint(
    ParserContext& parent,
    const ParserContext& nested,
    const std::string& prefix  // e.g., "battery_module"
) {
    // Line 362-365: Prefix all nested device names
    for (const auto& dev : nested.devices) {
        DeviceInstance prefixed = dev;
        prefixed.name = prefix + ":" + dev.name;  // "battery_module:bat"
        parent.devices.push_back(prefixed);
    }

    // Line 369-373: Prefix all connections
    for (const auto& conn : nested.connections) {
        Connection rewritten = conn;
        rewritten.from = prefix + ":" + conn.from;
        rewritten.to = prefix + ":" + conn.to;
        parent.connections.push_back(rewritten);
    }
}
```

### Bridge Node Structure

**File**: `src/jit_solver/components/blueprint_input.h` (25 lines)
```cpp
template <typename Provider = JitProvider>
class BlueprintInput {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;
    std::string exposed_type_str = "V";        // For type validation
    std::string exposed_direction_str = "In";  // For direction validation
    void execute(SimulationState& st, double dt);  // No-op pass-through
    void commit(SimulationState& st, double dt);   // No-op pass-through
};
```

**File**: `src/jit_solver/components/blueprint_output.h` (25 lines)
- Identical structure to BlueprintInput
- `exposed_direction_str = "Out"` by default

### Bridge Node Port Configuration

**File**: `src/jit_solver/components/port_registry.h`

**BlueprintInput Ports** (lines 210-213):
```cpp
constexpr const char* BlueprintInput_PORTS[] = {
    "ext",      // External port (parent-facing)
    "port"      // Internal port (child-facing)
};
```

**BlueprintOutput Ports** (lines 214-217):
```cpp
constexpr const char* BlueprintOutput_PORTS[] = {
    "ext",      // External port (parent-facing)
    "port"      // Internal port (child-facing)
};
```

**Port Directions** (lines 641-658):
```cpp
// BlueprintInput
constexpr RegistryPortDirection BlueprintInput_PORT_DIRECTIONS[] = {
    RegistryPortDirection::In,   // ext: parent reads from this
    RegistryPortDirection::Out   // port: child writes to this
};

// BlueprintOutput
constexpr RegistryPortDirection BlueprintOutput_PORT_DIRECTIONS[] = {
    RegistryPortDirection::Out,  // ext: parent writes to this
    RegistryPortDirection::In    // port: child reads from this
};
```

---

## 3. PORT TYPE AND DOMAIN HANDLING

### File: `src/json_parser/json_parser.cpp`
**Function**: `extract_exposed_ports()` (lines 377-417)

**Purpose**: Extract port metadata from BlueprintInput/BlueprintOutput devices

```cpp
std::unordered_map<std::string, Port> extract_exposed_ports(
    const ParserContext& blueprint
) {
    std::unordered_map<std::string, Port> exposed;

    for (const auto& dev : blueprint.devices) {
        if (dev.classname == "BlueprintInput" || dev.classname == "BlueprintOutput") {
            // Line 388: Device NAME is the exposed port name (e.g., "vin", "vout")
            std::string exposed_name = dev.name;

            // Line 391-397: Extract port direction from params
            Port port;
            auto dir_it = dev.params.find("exposed_direction");
            if (dir_it != dev.params.end()) {
                port.direction = (dir_it->second == "In") ? PortDirection::In : PortDirection::Out;
            } else {
                // Default direction based on component type
                port.direction = (dev.classname == "BlueprintInput") ? 
                    PortDirection::Out : PortDirection::In;
            }

            // Line 399-404: Extract port type from params
            auto type_it = dev.params.find("exposed_type");
            if (type_it != dev.params.end()) {
                port.type = parse_port_type(type_it->second);
            } else {
                port.type = PortType::Any;  // Default
            }

            exposed[exposed_name] = port;
        }
    }
    return exposed;
}
```

### Port Type Compatibility

**File**: `src/json_parser/json_parser.cpp`
**Function**: `are_ports_compatible()` (lines 196-199)

```cpp
static bool are_ports_compatible(PortType from_type, PortType to_type) {
    // PortType::Any is wildcard - compatible with everything
    if (from_type == PortType::Any || to_type == PortType::Any) {
        return true;
    }
    // ... other type-specific checks ...
}
```

### Port Type Parsing

**File**: `src/json_parser/json_parser.cpp`
**Function**: `parse_port_type()` (lines 166-178)

```cpp
static PortType parse_port_type(const std::string& s) {
    if (s == "V") return PortType::V;           // Voltage (0)
    if (s == "I") return PortType::I;           // Current (1)
    if (s == "Signal") return PortType::Any;    // Logical signal (7)
    if (s == "Fraction") return PortType::Any;  // Fraction (7)
    if (s == "Bool") return PortType::Bool;     // Boolean (2)
    if (s == "RPM") return PortType::RPM;       // RPM (3)
    if (s == "Temperature") return PortType::Temperature;
    if (s == "Pressure") return PortType::Pressure;
    if (s == "Position") return PortType::Position;
    if (s == "Any") return PortType::Any;       // Wildcard (7)
}
```

### Port Domain Mapping

**File**: `src/jit_solver/components/port_registry.h`
**Lines**: 645-648 and 659-662

```cpp
// BlueprintInput (both ports in Electrical domain)
constexpr uint8_t BlueprintInput_PORT_DOMAINS[] = {
    1,  // ext: Electrical (Domain::Electrical = 0b00001)
    1   // port: Electrical
};

// BlueprintOutput (both ports in Electrical domain)
constexpr uint8_t BlueprintOutput_PORT_DOMAINS[] = {
    1,  // ext: Electrical
    1   // port: Electrical
};
```

---

## 4. WIRE REWRITING FOR NESTED BLUEPRINTS

### File: `src/json_parser/json_parser.cpp`
**Section**: "Rewrite connections that point to expanded nested blueprints" (lines 546-626)

#### Parent Connection Rewriting Logic (lines 575-625)

**PARITY GUARD Comment** (lines 575-581):
```cpp
// === PARITY GUARD: Parent Connection Rewrite ===
// INVARIANT: This rewrite is CANONICAL for parent-facing composite ports.
// - Blueprint expanded from TypeRegistry: device_name + ":" + port_name becomes exposed.
// - Parent connections (editor/root) use format: instance:port.ext (expanded side).
// - Internal connections (within expanded blueprint) use format: instance:port.port.
// - Root/editor resolver must map expandable root endpoints to :instance:port.ext format.
// - AOT and JIT solvers must agree on bridge semantics (.ext vs .port union).
```

#### Detection of Expanded Blueprints (lines 552-568)

```cpp
std::set<std::string> expanded_blueprint_names;
expanded_blueprint_names.insert(expanded_instance_names.begin(), expanded_instance_names.end());

// Find all expanded blueprints by looking for BlueprintInput/BlueprintOutput devices
// Device names like "lamp_bp:vin" and "lamp_bp:vout"
for (const auto& dev : ctx.devices) {
    if (dev.classname == "BlueprintInput" || dev.classname == "BlueprintOutput") {
        // Extract blueprint name (everything before the colon)
        size_t colon_pos = dev.name.find(':');
        if (colon_pos != std::string::npos) {
            std::string blueprint_name = dev.name.substr(0, colon_pos);
            expanded_blueprint_names.insert(blueprint_name);
        }
    }
}
```

#### Connection Rewriting Algorithm (lines 584-620)

```cpp
auto rewrite_port = [&](std::string& port_ref) {
    size_t dot_pos = port_ref.find('.');
    if (dot_pos == std::string::npos) return;

    std::string device_name = port_ref.substr(0, dot_pos);
    std::string port_name = port_ref.substr(dot_pos + 1);

    // Skip if already has prefix (internal connection)
    if (device_name.find(':') != std::string::npos) {
        return;
    }

    // Check if this device is an expanded blueprint
    if (expanded_blueprint_names.count(device_name)) {
        // Rewrite parent-facing composite ports to the bridge node's external side:
        // "lamp_bp.vin" -> "lamp_bp:vin.ext"
        std::string old_ref = port_ref;
        port_ref = device_name + ":" + port_name + ".ext";  // LINE 616
        spdlog::info("[json_parser] Rewrote parent connection: '{}' -> '{}'",
                    old_ref, port_ref);
    }
};

rewrite_port(conn.from);
rewrite_port(conn.to);
```

#### Key Transformation Rule
```
Parent Connection Format:     device.port
    ↓ (if device is expanded blueprint)
Bridge Connection Format:     device:port.ext
                                      ^^^ external side for parent
                              device:port.port
                                      ^^^^ internal side for child
```

---

## 5. SIGNAL UNIFICATION (JIT PATH)

### File: `src/jit_solver/jit_solver.cpp`
**Section**: "BlueprintInput/Output Bridge Union" (lines 267-286)

**PARITY GUARD Comment** (lines 267-274):
```cpp
// === PARITY GUARD: BlueprintInput/Output Bridge Union ===
// INVARIANT: ext↔port union MUST be mirrored in AOT codegen.
// - BlueprintInput/BlueprintOutput bridge nodes have two ports:
//   .ext (external, parent-facing) and .port (internal, child-facing).
// - These ports must be unified into a single signal to implement transparent passthrough.
// - Parser rewrite ensures parent connections use :instance:port.ext format.
// - JIT (this path) and AOT (codegen.cpp) must unify these identically.
```

#### Union-Find Unification Logic

```cpp
for (const auto& dev : devices) {
    if (dev.visual_only) continue;
    if (dev.classname == "BlueprintInput" || dev.classname == "BlueprintOutput") {
        std::string ext_key  = dev.name + ".ext";   // e.g., "lamp_bp:vin.ext"
        std::string port_key = dev.name + ".port";  // e.g., "lamp_bp:vin.port"
        
        auto it_ext  = port_to_idx.find(ext_key);
        auto it_port = port_to_idx.find(port_key);
        
        if (it_ext != port_to_idx.end() && it_port != port_to_idx.end()) {
            uf.unite(it_ext->second, it_port->second);  // Merge to single signal
        }
    }
}
```

---

## 6. SIGNAL UNIFICATION (AOT PATH)

### File: `src/codegen/codegen.cpp`
**Section**: "BlueprintInput/Output Bridge Union" (lines 1088-1104)

**PARITY GUARD Comment** (lines 1088-1093):
```cpp
// === PARITY GUARD: BlueprintInput/Output Bridge Union ===
// INVARIANT: ext↔port union MUST match JIT solver's logic.
// [CRITICAL] This code must remain in sync with jit_solver.cpp bridge unification.
// - Both paths (JIT in jit_solver.cpp:271-289, AOT here) must unify .ext and .port identically.
// - Parser rewrite ensures parent connections use :instance:port.ext format.
// - Failure to mirror this will cause JIT/AOT divergence for composite blueprints.
```

#### Identical Unification in AOT

```cpp
for (const auto& dev : expanded.devices) {
    if (dev.classname == "BlueprintInput" || dev.classname == "BlueprintOutput") {
        std::string ext_key  = dev.name + ".ext";
        std::string port_key = dev.name + ".port";
        auto it_ext  = port_to_idx.find(ext_key);
        auto it_port = port_to_idx.find(port_key);
        if (it_ext != port_to_idx.end() && it_port != port_to_idx.end()) {
            uf_unite(it_ext->second, it_port->second);
        }
    }
}
```

---

## 7. TEST COVERAGE

### File: `tests/test_aot_composite.cpp`

#### Test: Bridge Signal Unification (lines 1161-1257)
```cpp
// Regression: AOT codegen must unify BlueprintInput/BlueprintOutput ext↔port
TEST(CompositeBlueprint, BridgePortUnification)
```

**Test Setup**:
- Creates BlueprintInput with two ports: port (Out, Any), ext (In, Any)
- Creates BlueprintOutput with two ports: port (In, Any), ext (Out, Any)
- Embeds both into composite blueprint
- Verifies both JIT and AOT unify ext↔port into same signal

**Assertions**:
```cpp
EXPECT_EQ(jit_result.port_to_signal[bridge_in + ".ext"],
          jit_result.port_to_signal[bridge_in + ".port"])
    << "JIT must unify BlueprintInput ext↔port";

EXPECT_EQ(jit_result.port_to_signal[bridge_out + ".ext"],
          jit_result.port_to_signal[bridge_out + ".port"])
    << "JIT must unify BlueprintOutput ext↔port";
```

---

## 8. BLUEPRINT PORT METADATA PARAMETERS

### Supported Parameters on BlueprintInput/BlueprintOutput

From `extract_exposed_ports()` (lines 392-404):

| Parameter | Purpose | Type | Example |
|-----------|---------|------|---------|
| `exposed_type` | Overrides port type | String | `"V"`, `"Signal"`, `"Bool"`, etc. |
| `exposed_direction` | Overrides port direction | String | `"In"` or `"Out"` |

**Default Behavior**:
- **BlueprintInput**: Direction Out (parent reads), Type Any
- **BlueprintOutput**: Direction In (parent writes), Type Any

---

## Summary Flow Diagram

```
JSON Input with Nested Blueprint
    ↓
parse_json_impl() [Line 419]
    ├─ Detect blueprint type: !cpp_class && devices not empty
    ├─ Cycle detection via expanding set
    └─ Recursive call to parse_json_impl()
         ↓
    Expanded ParserContext
         ↓
    merge_nested_blueprint() [Line 353]
         ├─ Prefix device names: "bat" → "battery:bat"
         ├─ Prefix connections
         └─ Result includes BlueprintInput/Output devices
              ↓
    Top-level connection rewriting [Line 551]
         ├─ Detect expanded blueprints via BlueprintInput/Output
         └─ Rewrite parent connections
              "lamp.vin" → "lamp:vin.ext" [Line 616]
              ↓
    build_systems_dev() (JIT) / codegen (AOT)
         ├─ Extract .ext and .port signal indices
         ├─ Union-find unite() .ext ↔ .port [JIT Line 283, AOT Line 1101]
         └─ Single signal index for bridge nodes
```

---

## Cross-Reference Map

| Concept | JIT File | JIT Line(s) | AOT File | AOT Line(s) |
|---------|----------|------------|----------|------------|
| Bridge Port Structure | components/blueprint_input.h | 8-25 | - | - |
| Port Registry | components/port_registry.h | 210-217, 641-667 | - | - |
| Bridge Union | jit_solver.cpp | 267-286 | codegen.cpp | 1088-1104 |
| Parser Expansion | json_parser.cpp | 419-681 | - | - |
| Connection Rewrite | json_parser.cpp | 546-626 | - | - |
| Port Type Handling | json_parser.cpp | 166-404 | - | - |
| Domain Mapping | port_registry.h | 645-667 | - | - |

