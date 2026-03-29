# Phase 3: Blueprint & Build Pipeline

## Methodology: Failing-Test-First

Every step follows red-green TDD:
1. Write test(s) - they MUST fail (red)
2. Implement - tests MUST pass (green)
3. Phase DONE only when `cd build && ctest` reports 0 failures

## Overview

Update the build pipeline to support push propagation:
- Wire name -> address mapping (already exists via union-find, keep as-is)
- Topological sort within bucket 2 (consumers)
- One-source-per-wire validation at build time
- Initial values in blueprints
- Remove execution phase metadata from `.blueprint` files

---

## Step 3.1: One-Source-Per-Wire Validation

### Test First (RED)

Create `tests/test_push_build_validation.cpp`:

```cpp
#include <gtest/gtest.h>
#include "jit_solver/jit_solver.h"
#include "json_parser/json_parser.h"

TEST(PushBuildValidation, SingleSourcePerWireOK) {
    // Battery writes to bus, Load reads from bus - valid
    std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery",
             "params": {"v_nominal": "24.0"}, "ports": ["v_out", "v_in"]},
            {"name": "load", "classname": "Load",
             "params": {"conductance": "0.1"}, "ports": ["input"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"}, "ports": ["v"]}
        ],
        "connections": [["bat.v_out", "load.input"], ["bat.v_in", "gnd.v"]]
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> conns;
    for (auto& c : ctx.connections) conns.push_back({c.from, c.to});

    // Should NOT throw
    EXPECT_NO_THROW(build_systems_dev(ctx.devices, conns));
}

TEST(PushBuildValidation, MultipleSourcesSameWireErrors) {
    // Battery AND Generator both write to same wire - INVALID
    std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery",
             "params": {"v_nominal": "24.0"}, "ports": ["v_out", "v_in"]},
            {"name": "gen", "classname": "Generator",
             "params": {"v_nominal": "28.5"}, "ports": ["v_out", "v_in"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"}, "ports": ["v"]}
        ],
        "connections": [
            ["bat.v_out", "gen.v_out"],
            ["bat.v_in", "gnd.v"],
            ["gen.v_in", "gnd.v"]
        ]
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> conns;
    for (auto& c : ctx.connections) conns.push_back({c.from, c.to});

    // Should throw or return error: two sources on same wire
    EXPECT_THROW(build_systems_dev(ctx.devices, conns), std::runtime_error);
}

TEST(PushBuildValidation, MaxSelectorAllowsMultipleSources) {
    // Battery and Generator feed into MaxSelector - VALID
    // MaxSelector arbitrates and writes to bus
    std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery",
             "params": {"v_nominal": "24.0"}, "ports": ["v_out", "v_in"]},
            {"name": "gen", "classname": "Generator",
             "params": {"v_nominal": "28.5"}, "ports": ["v_out", "v_in"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"}, "ports": ["v"]}
        ],
        "connections": [
            ["bat.v_in", "gnd.v"],
            ["gen.v_in", "gnd.v"]
        ]
    })";
    // Note: bat.v_out and gen.v_out go to DIFFERENT wires (not connected)
    // A MaxSelector would read both and write to a third wire

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> conns;
    for (auto& c : ctx.connections) conns.push_back({c.from, c.to});

    EXPECT_NO_THROW(build_systems_dev(ctx.devices, conns));
}
```

### Implementation (GREEN)

In `src/jit_solver/jit_solver.cpp`, in `build_systems_dev()`, after signal allocation:

```cpp
// === One-source-per-wire validation ===
// Track which signal indices are written by source components
std::unordered_map<uint32_t, std::string> signal_writers; // signal_idx -> "component.port"

for (const auto& dev : devices) {
    bool is_source = (dev.classname == "Battery" || dev.classname == "Generator" ||
                      dev.classname == "GS24" || dev.classname == "RefNode" ||
                      dev.classname == "ControlledVoltageSource" ||
                      dev.classname == "ControlledCurrentSource");
    if (!is_source) continue;

    // Check each output port of this source
    for (const auto& port_name : get_output_ports(dev.classname)) {
        std::string port_key = dev.name + "." + port_name;
        auto it = port_to_signal.find(port_key);
        if (it == port_to_signal.end()) continue;

        uint32_t sig = it->second;
        auto existing = signal_writers.find(sig);
        if (existing != signal_writers.end()) {
            throw std::runtime_error(
                "Wire has multiple sources: " + existing->second +
                " and " + port_key + ". Use MaxSelector.");
        }
        signal_writers[sig] = port_key;
    }
}
```

Helper function to identify output ports per component type:
```cpp
static std::vector<std::string> get_output_ports(const std::string& classname) {
    if (classname == "Battery") return {"v_out"};
    if (classname == "Generator") return {"v_out"};
    if (classname == "GS24") return {"v_out"};
    if (classname == "RefNode") return {"v"};
    if (classname == "ControlledVoltageSource") return {"v_pos"};
    if (classname == "ControlledCurrentSource") return {"v_pos"};
    return {};
}
```

---

## Step 3.2: Topological Sort for Consumers

### Test First (RED)

Add to `tests/test_push_build_validation.cpp`:

```cpp
TEST(PushBuildValidation, TopologicalSortRespectsDataDependency) {
    // Battery -> Switch -> Load
    // Switch must execute after Battery (reads v_in from Battery)
    // Load must execute after Switch (reads from Switch output)
    std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery",
             "params": {"v_nominal": "24.0"}, "ports": ["v_out", "v_in"]},
            {"name": "sw", "classname": "Switch",
             "params": {}, "ports": ["v_in", "v_out", "control", "state"]},
            {"name": "load", "classname": "Load",
             "params": {"conductance": "0.1"}, "ports": ["input"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"}, "ports": ["v"]}
        ],
        "connections": [
            ["bat.v_out", "sw.v_in"],
            ["sw.v_out", "load.input"],
            ["bat.v_in", "gnd.v"]
        ]
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> conns;
    for (auto& c : ctx.connections) conns.push_back({c.from, c.to});

    auto result = build_systems_dev(ctx.devices, conns);

    // After one step, load should see battery voltage through closed switch
    SimulationState st;
    for (uint32_t i = 0; i < result.signal_count; i++) {
        st.allocate_signal(0.0f, {Domain::Electrical, false});
    }

    // Close the switch manually
    auto& sw = std::get<Switch<JitProvider>>(result.devices["sw"]);
    sw.closed = true;

    result.scheduler.step(st, 1.0f / 60.0f);

    float v_load = st.values[result.port_to_signal["load.input"]];
    EXPECT_NEAR(v_load, 24.0f, 1.0f);
}

TEST(PushBuildValidation, CycleDetectionWarns) {
    // Feedback loop: GSC -> Generator -> VoltageSense -> GSC
    // Should NOT throw (feedback loops are handled via one-frame delay)
    // But should log a warning about the cycle
    // Test just verifies it doesn't crash
    std::string json = R"({
        "devices": [
            {"name": "gen", "classname": "Generator",
             "params": {"v_nominal": "28.5"}, "ports": ["v_out", "v_in"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"}, "ports": ["v"]}
        ],
        "connections": [["gen.v_in", "gnd.v"]]
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> conns;
    for (auto& c : ctx.connections) conns.push_back({c.from, c.to});

    EXPECT_NO_THROW(build_systems_dev(ctx.devices, conns));
}
```

### Implementation (GREEN)

In `build_systems_dev()`, after classifying components into source/consumer:

```cpp
// === Topological sort for consumers ===
// Build adjacency: component A -> component B if A writes to a signal B reads

// 1. For each component, identify read signals (input ports) and write signals (output ports)
struct ComponentIO {
    std::string name;
    std::vector<uint32_t> reads;   // signal indices this component reads
    std::vector<uint32_t> writes;  // signal indices this component writes
};

// 2. Build dependency graph: edge from writer to reader
std::unordered_map<std::string, std::vector<std::string>> adj;
std::unordered_map<std::string, int> in_degree;

// 3. Kahn's algorithm for topological sort
std::queue<std::string> ready;
for (auto& [name, deg] : in_degree) {
    if (deg == 0) ready.push(name);
}

std::vector<std::string> sorted_order;
while (!ready.empty()) {
    auto name = ready.front(); ready.pop();
    sorted_order.push_back(name);
    for (auto& dep : adj[name]) {
        if (--in_degree[dep] == 0) ready.push(dep);
    }
}

// 4. If sorted_order.size() < consumers.size(), there's a cycle -> warn (don't error)
if (sorted_order.size() < consumer_names.size()) {
    spdlog::warn("[build] Dependency cycle detected in consumers. "
                 "Using one-frame delay for feedback paths.");
    // Add remaining components in original order
}

// 5. Add consumers to scheduler in topological order
for (auto& name : sorted_order) {
    auto& variant = result.devices[name];
    std::visit([&](auto& comp) {
        result.scheduler.add_consumer(&comp);
    }, variant);
}
```

---

## Step 3.3: Initial Values in Blueprint

### Test First (RED)

```cpp
TEST(PushBuildValidation, InitialValuesFromBlueprint) {
    // Blueprint specifies initial value for a wire
    std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery",
             "params": {"v_nominal": "24.0"}, "ports": ["v_out", "v_in"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"}, "ports": ["v"]}
        ],
        "connections": [["bat.v_in", "gnd.v"]],
        "initial_values": {
            "bat.v_out": 24.0
        }
    })";

    JIT_Simulator sim;
    sim.start_from_json(json);

    // Before any step, initial value should be set
    float v = sim.get_port_value("bat", "v_out");
    EXPECT_NEAR(v, 24.0f, 0.1f);
}
```

### Implementation (GREEN)

In `parse_json()`, parse optional `"initial_values"` object:
```cpp
// In json_parser.cpp, after parsing devices and connections:
if (json.contains("initial_values")) {
    for (auto& [port_ref, value] : json["initial_values"].items()) {
        ctx.initial_values[port_ref] = value.get<float>();
    }
}
```

In `Simulator::start_from_json()`, after allocating signals:
```cpp
// Apply initial values
if (!ctx.initial_values.empty()) {
    for (auto& [port_ref, value] : ctx.initial_values) {
        auto it = build_result_->port_to_signal.find(port_ref);
        if (it != build_result_->port_to_signal.end()) {
            state_.values[it->second] = value;
        }
    }
}
```

---

## Step 3.4: Remove Execution Phase Metadata from Blueprints

### Test First (RED)

```cpp
TEST(PushBuildValidation, NoExecutionPhasesRequired) {
    // Blueprint without execution phases should work fine
    std::string json = R"({
        "devices": [
            {"name": "bat", "classname": "Battery",
             "params": {"v_nominal": "24.0"}, "ports": ["v_out", "v_in"]},
            {"name": "gnd", "classname": "RefNode",
             "params": {"value": "0.0"}, "ports": ["v"]}
        ],
        "connections": [["bat.v_in", "gnd.v"]]
    })";
    // Note: no "execution" field in device definitions

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> conns;
    for (auto& c : ctx.connections) conns.push_back({c.from, c.to});

    EXPECT_NO_THROW(build_systems_dev(ctx.devices, conns));
}
```

### Implementation (GREEN)

In `json_parser.cpp`:
- Make `execution` field optional (it already is via `std::optional<ExecutionPhases>`)
- Remove the `throw` in `get_execution_traits()` when execution is missing
- `build_systems_dev()` no longer calls `get_execution_traits()` at all

In `.blueprint` files (all 80+):
- The `execution` field can be left as-is (ignored) or removed
- No urgency to modify blueprint files - the parser just ignores the field

---

## Step 3.5: MaxSelector Component

### Test First (RED)

```cpp
TEST(PushComponents, MaxSelectorPicksHighestVoltage) {
    SimulationState st;
    uint32_t src1 = st.allocate_signal(24.0f, {Domain::Electrical, false});
    uint32_t src2 = st.allocate_signal(28.5f, {Domain::Electrical, false});
    uint32_t out = st.allocate_signal(0.0f, {Domain::Electrical, false});

    // MaxSelector is a new component
    MaxSelector<JitProvider> ms;
    ms.provider.set(PortNames::a, src1);
    ms.provider.set(PortNames::b, src2);
    ms.provider.set(PortNames::output, out);

    ms.solve_electrical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[out], 28.5f);
}

TEST(PushComponents, MaxSelectorWithOneSourceZero) {
    SimulationState st;
    uint32_t src1 = st.allocate_signal(24.0f, {Domain::Electrical, false});
    uint32_t src2 = st.allocate_signal(0.0f, {Domain::Electrical, false});
    uint32_t out = st.allocate_signal(0.0f, {Domain::Electrical, false});

    MaxSelector<JitProvider> ms;
    ms.provider.set(PortNames::a, src1);
    ms.provider.set(PortNames::b, src2);
    ms.provider.set(PortNames::output, out);

    ms.solve_electrical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[out], 24.0f);
}
```

### Implementation (GREEN)

Add to `all.h`:
```cpp
/// MaxSelector - selects the highest voltage from two sources
/// Used for multi-source bus arbitration (replaces implicit legacy bus coupling)
template <typename Provider = JitProvider>
class MaxSelector {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;

    MaxSelector() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load() {}
};
```

Add to `all.cpp`:
```cpp
template <typename Provider>
void MaxSelector<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float v1 = st.values[provider.get(PortNames::a)];
    float v2 = st.values[provider.get(PortNames::b)];
    st.values[provider.get(PortNames::output)] = (v1 > v2) ? v1 : v2;
}
```

Add `PortNames::a` and `PortNames::b` if not already present in port_registry.h.

Add `MaxSelector` to `ComponentVariant` in port_registry.h.

Add `MaxSelector` to the library: `library/electrical/MaxSelector.blueprint`.

---

## Files Changed Summary

| File | Action |
|------|--------|
| `src/jit_solver/jit_solver.cpp` | Add source validation, topological sort, scheduler population |
| `src/jit_solver/jit_solver.h` | BuildResult uses PushScheduler instead of PhaseComponents |
| `src/json_parser/json_parser.h` | Add `initial_values` to ParserContext |
| `src/json_parser/json_parser.cpp` | Parse `initial_values` from JSON |
| `src/jit_solver/simulator.cpp` | Apply initial values in start_from_json() |
| `src/jit_solver/components/all.h` | Add MaxSelector |
| `src/jit_solver/components/all.cpp` | Implement MaxSelector |
| `src/jit_solver/components/port_registry.h` | Add MaxSelector to ComponentVariant, add port names a/b |
| `library/electrical/MaxSelector.blueprint` | NEW: MaxSelector blueprint definition |
| `tests/test_push_build_validation.cpp` | NEW: validation tests |
| `tests/CMakeLists.txt` | Add test target |

## Completion Criteria

- [ ] One-source-per-wire validation catches conflicting sources at build time
- [ ] Topological sort orders consumers correctly
- [ ] Cycles detected and warned (not errored) - one-frame delay handles them
- [ ] Initial values parsed from blueprints and applied before first step
- [ ] MaxSelector component works for multi-source bus arbitration
- [ ] Execution phase metadata ignored (not required)
- [ ] `cd build && ctest` reports 0 failures
