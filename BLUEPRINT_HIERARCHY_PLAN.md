# Hierarchical Blueprints Implementation Plan

## Overview
Add support for nested blueprints (composite components) with BlueprintInput/BlueprintOutput port markers.

## Current State
- ✅ Port system with type validation (PortType: V, I, Bool, RPM, etc.)
- ✅ PortDirection (In, Out, InOut)
- ✅ `SubsystemCall`, `SystemTemplate` defined in parser but NOT used
- ✅ `is_composite`, `template_name` fields exist but NOT implemented
- ❌ No BlueprintInput/BlueprintOutput components
- ❌ No blueprint loading/flattening logic

## Goal
Enable hierarchical blueprints where:
1. A blueprint can contain nested blueprints (recursive)
2. BlueprintInput/BlueprintOutput mark exposed ports
3. Parser flattens nested blueprints at load time
4. Editor shows collapsed node with exposed ports, expands on double-click

---

## Phase 1: BlueprintInput/BlueprintOutput Components

### 1.1 JSON Definitions

**File:** `components/BlueprintInput.json`
```json
{
  "classname": "BlueprintInput",
  "description": "Input port marker - exposes internal signal to parent blueprint as input port",
  "default_ports": {
    "port": {
      "direction": "Out",
      "alias": "dynamic",
      "type": "Any"
    }
  },
  "default_params": {
    "exposed_type": "V",
    "exposed_direction": "In"
  },
  "default_domains": ["Electrical"],
  "default_priority": "high",
  "default_critical": false,
  "default_size": {"x": 1, "y": 1}
}
```

**File:** `components/BlueprintOutput.json`
```json
{
  "classname": "BlueprintOutput",
  "description": "Output port marker - exposes internal signal to parent blueprint as output port",
  "default_ports": {
    "port": {
      "direction": "In",
      "alias": "dynamic",
      "type": "Any"
    }
  },
  "default_params": {
    "exposed_type": "V",
    "exposed_direction": "Out"
  },
  "default_domains": ["Electrical"],
  "default_priority": "high",
  "default_critical": false,
  "default_size": {"x": 1, "y": 1}
}
```

### 1.2 C++ Implementation

**File:** `src/jit_solver/components/all.h`
```cpp
/// BlueprintInput - input port marker for nested blueprints
template <typename Provider = JitProvider>
class BlueprintInput {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;

    // Pass-through parameters for exposed port metadata
    std::string exposed_type_str = "V";      // For type validation
    std::string exposed_direction_str = "In";  // For direction validation

    BlueprintInput() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};

/// BlueprintOutput - output port marker for nested blueprints
template <typename Provider = JitProvider>
class BlueprintOutput {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;

    // Pass-through parameters for exposed port metadata
    std::string exposed_type_str = "V";      // For type validation
    std::string exposed_direction_str = "Out";  // For direction validation

    BlueprintOutput() = default;

    void solve_electrical(an24::SimulationState& st, float dt);
};
```

**File:** `src/jit_solver/components/all.cpp`
```cpp
template <typename Provider>
void BlueprintInput<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // No-op - pass-through component (like Bus)
    // Union-find will collapse port to connected signal
}

template <typename Provider>
void BlueprintOutput<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // No-op - pass-through component (like Bus)
    // Union-find will collapse port to connected signal
}
```

### 1.3 Tests

**File:** `tests/test_blueprint_ports.cpp`
```cpp
TEST(BlueprintInput, PassThroughLikeBus) {
    // BlueprintInput should behave like Bus (no-op)
    SimulationState st;
    st.add_signal(/*...*/);

    BlueprintInput<JitProvider> input;
    input.provider.init(/*...*/);

    // Should not crash, should not modify state
    ASSERT_NO_THROW(input.solve_electrical(st, 0.016f));
}

TEST(BlueprintOutput, PassThroughLikeBus) {
    // BlueprintOutput should behave like Bus (no-op)
    // Same test as BlueprintInput
}
```

---

## Phase 2: Blueprint Loading & Fallback

### 2.1 Parser Modification

**File:** `src/jit_solver/jit_solver.cpp` (or `build_systems`)
```cpp
class Systems {
    // ... existing code ...

    /// Load component from registry or fallback to blueprint
    const ComponentDefinition* load_component_or_blueprint(
        const std::string& classname,
        const ComponentRegistry& registry
    ) {
        // Try C++ registry first
        if (auto* comp = registry.get(classname)) {
            return comp;
        }

        // Fallback to blueprint file
        std::string blueprint_path = "blueprints/" + classname + ".json";
        if (std::filesystem::exists(blueprint_path)) {
            spdlog::info("[build] Found blueprint for {}", classname);
            return load_blueprint_definition(blueprint_path, registry);
        }

        return nullptr;
    }
};
```

### 2.2 Blueprint Definition Structure

**File:** `src/json_parser/json_parser.h`
```cpp
/// Blueprint definition (loaded from .json file)
struct BlueprintDefinition {
    std::string name;         // Human-readable name
    std::string description;
    std::vector<DeviceInstance> devices;
    std::vector<Connection> connections;

    // Exposed ports (derived from BlueprintInput/BlueprintOutput)
    std::unordered_map<std::string, Port> exposed_ports;
};
```

### 2.3 Tests

**File:** `tests/test_blueprint_loading.cpp`
```cpp
TEST(BlueprintLoading, FallsBackToFileWhenNotInRegistry) {
    // Create temporary blueprint file
    // Try to load it via load_component_or_blueprint
    // Should load successfully
}

TEST(BlueprintLoading, ReturnsNullForUnknownComponent) {
    // Try to load non-existent component
    // Should return nullptr
}
```

---

## Phase 3: Flattening Algorithm

### 3.1 Core Flattening Function

**File:** `src/json_parser/blueprint_flattener.cpp` (new file)
```cpp
class BlueprintFlattener {
public:
    /// Recursively flatten nested blueprints into flat device list
    static void flatten(
        const std::vector<DeviceInstance>& input_devices,
        const std::vector<Connection>& input_connections,
        std::vector<DeviceInstance>& output_devices,
        std::vector<Connection>& output_connections,
        const ComponentRegistry& registry
    );

private:
    /// Process single composite device
    static void process_composite_device(
        const DeviceInstance& composite,
        const std::string& name_prefix,  // e.g., "parent:"
        std::vector<DeviceInstance>& output_devices,
        std::vector<Connection>& output_connections,
        const ComponentRegistry& registry
    );

    /// Find exposed ports in blueprint (BlueprintInput/BlueprintOutput)
    static std::unordered_map<std::string, Port> extract_exposed_ports(
        const std::vector<DeviceInstance>& devices
    );
};
```

### 3.2 Flattening Algorithm

```cpp
void BlueprintFlattener::flatten(
    const std::vector<DeviceInstance>& input_devices,
    const std::vector<Connection>& input_connections,
    std::vector<DeviceInstance>& output_devices,
    std::vector<Connection>& output_connections,
    const ComponentRegistry& registry
) {
    // Pass 1: Copy non-composite devices
    for (const auto& dev : input_devices) {
        if (!dev.is_composite) {
            output_devices.push_back(dev);
        }
    }

    // Pass 2: Expand composite devices recursively
    for (const auto& dev : input_devices) {
        if (dev.is_composite) {
            process_composite_device(dev, dev.name + ":",
                                   output_devices, output_connections, registry);
        }
    }

    // Pass 3: Rewrite connections
    // Replace "composite.exposed_port" with "composite:nested_device.port"
    for (const auto& conn : input_connections) {
        Connection rewritten = rewrite_connection(conn, /* ... */);
        output_connections.push_back(rewritten);
    }
}
```

### 3.3 Example Transformation

**Before (with nested blueprint):**
```json
{
  "devices": [
    {"classname": "battery_module", "name": "bat1", "is_composite": true}
  ],
  "connections": [
    {"from": "gnd.v", "to": "bat1:v_in"}
  ]
}
```

**After (flattened):**
```json
{
  "devices": [
    {"classname": "BlueprintInput", "name": "bat1:v_in", ...},
    {"classname": "Battery", "name": "bat1:main_battery", ...},
    {"classname": "BlueprintOutput", "name": "bat1:v_out", ...}
  ],
  "connections": [
    {"from": "gnd.v", "to": "bat1:v_in.port"},
    {"from": "bat1:v_in.port", "to": "bat1:main_battery.v_in"},
    {"from": "bat1:main_battery.v_out", "to": "bat1:v_out.port"}
  ]
}
```

### 3.4 Tests

**File:** `tests/test_blueprint_flattening.cpp`
```cpp
TEST(BlueprintFlattening, SingleLevelNesting) {
    // Input: blueprint with 1 composite device
    // Expected: flattened with prefix "composite:"
}

TEST(BlueprintFlattening, ExposesPortsCorrectly) {
    // Blueprint with BlueprintInput/BlueprintOutput
    // Expected: exposed_ports populated
}

TEST(BlueprintFlattening, RecursiveNesting) {
    // Blueprint → blueprint → component
    // Expected: fully flattened with "a:b:c:" prefixes
}

TEST(BlueprintFlattening, RewiresConnections) {
    // Connection to composite:exposed_port
    // Expected: Rewired to composite:nested_device.port
}
```

---

## Phase 4: Integration with build_systems

### 4.1 Modify JIT_Solver::build()

**File:** `src/jit_solver/jit_solver.cpp`
```cpp
template <>
void Simulator<JIT_Solver>::build(const ParserContext& ctx) {
    // Step 1: Flatten blueprints
    ParserContext flattened_ctx;
    BlueprintFlattener::flatten(
        ctx.devices,
        ctx.connections,
        flattened_ctx.devices,
        flattened_ctx.connections,
        ctx.component_registry
    );

    // Step 2: Build from flattened context (existing logic)
    // ... rest of build() ...
}
```

### 4.2 Integration Tests

```cpp
TEST(JITBuild, LoadsAndFlattensNestedBlueprint) {
    // Load blueprint.json with nested blueprint
    // Build should succeed
    // Simulation should run
}
```

---

## Phase 5: Editor Support (Future)

### 5.1 Collapsed Node Rendering

- Show nested blueprint as single node
- Render exposed ports on node boundary
- Port colors based on exposed_type

### 5.2 Drill-down Navigation

- Double-click → expand nested blueprint
- Breadcrumb navigation: `Root → Parent → Child`
- Back button to return to parent

### 5.3 JSON Structure for Storage

```json
{
  "devices": [
    {
      "classname": "my_battery",
      "name": "bat1",
      "is_composite": true,
      "collapsed": true,  // Editor state
      "position": {"x": 10, "y": 5}
    }
  ]
}
```

---

## Implementation Order (Failing Tests First)

1. ✅ **Write test for BlueprintInput pass-through** → FAIL (no component)
2. ✅ **Create BlueprintInput.json** → FAIL (no C++ class)
3. ✅ **Implement BlueprintInput C++** → PASS
4. ✅ **Write test for BlueprintOutput** → FAIL
5. ✅ **Create BlueprintOutput.json** → FAIL
6. ✅ **Implement BlueprintOutput C++** → PASS
7. ✅ **Write test for blueprint loading** → FAIL
8. ✅ **Implement load_component_or_blueprint()** → FAIL
9. ✅ **Implement blueprint fallback** → PASS
10. ✅ **Write test for single-level flattening** → FAIL
11. ✅ **Implement BlueprintFlattener** → FAIL
12. ✅ **Implement flatten()** → FAIL
13. ✅ **Implement extract_exposed_ports()** → PASS
14. ✅ **Write test for recursive flattening** → FAIL
15. ✅ **Implement recursive flattening** → PASS
16. ✅ **Write integration test with JIT build** → FAIL
17. ✅ **Integrate with build_systems** → PASS

---

## Open Questions

1. **Where to store blueprint files?**
   - Proposal: `blueprints/` directory (parallel to `components/`)

2. **Blueprint caching?**
   - Parse once, cache flattened result
   - Re-parse on file change

3. **Circular dependency detection?**
   - Skip for now (as discussed)

4. **Editor-only state storage?**
   - Where to store `collapsed`, `position` for editor?
   - Proposal: Separate `.editor.json` or embedded in main JSON

---

## Success Criteria

- ✅ Can create nested blueprints with BlueprintInput/BlueprintOutput
- ✅ Parser flattens nested blueprints at load time
- ✅ JIT/AOT builds from flattened blueprint
- ✅ Simulation works correctly with nested blueprints
- ✅ Exposed ports work with type validation
- ⏳ Editor shows collapsed nodes (future)

---

## Files to Create

1. `components/BlueprintInput.json`
2. `components/BlueprintOutput.json`
3. `src/json_parser/blueprint_flattener.h`
4. `src/json_parser/blueprint_flattener.cpp`
5. `tests/test_blueprint_ports.cpp`
6. `tests/test_blueprint_loading.cpp`
7. `tests/test_blueprint_flattening.cpp`

## Files to Modify

1. `src/jit_solver/components/all.h` - Add BlueprintInput/BlueprintOutput
2. `src/jit_solver/components/all.cpp` - Implement solve_electrical
3. `src/jit_solver/components/explicit_instantiations.h` - Add template instantiations
4. `src/jit_solver/jit_solver.cpp` - Add blueprint loading
5. `src/jit_solver/simulator.cpp` - Call flattening in build()
6. `src/json_parser/json_parser.h` - Add BlueprintDefinition
