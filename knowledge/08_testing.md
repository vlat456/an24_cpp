# Testing

## Test Framework

Uses Google Test (gtest). Tests are organized by feature area in `tests/`.

> **Note:** This document references the legacy solver API (`st.across`, `st.through`, `st.conductance`). The current push runtime uses `st.values[]` only. See `tests/test_architecture_regression.cpp` for current test patterns.

## Running Tests

```bash
# Run all tests
cd build && ctest

# Run specific test executable
./build/tests/editor_data_tests

# Run with regex filter
ctest -R "editor_data" --output-on-failure

# Run specific test case
./build/tests/editor_data_tests --gtest_filter="DataTest.TestName"
```

## Current Test Pattern

```cpp
#include <gtest/gtest.h>
#include "core/solvers/jit/components/all.h"
#include "core/solvers/common/port_registry.h"

// Helper factory
static AND<JitProvider> make_and() {
    AND<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);
    return comp;
}

static SimulationState make_state() {
    SimulationState st;
    st.allocate_signal(0.0f, {Domain::Logical, false});  // A
    st.allocate_signal(0.0f, {Domain::Logical, false}); // B
    st.allocate_signal(0.0f, {Domain::Logical, false});  // out
    return st;
}

TEST(ANDTest, BothTrue_ReturnsTrue) {
    auto comp = make_and();
    auto st = make_state();
    
    st.values[0] = 1.0f;  // A = TRUE
    st.values[1] = 1.0f;  // B = TRUE
    
    comp.execute(st, 1.0/60.0);
    
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);
}
```

## Test Categories

### Component Tests
Test individual component behavior:
- `test_logic_gates.cpp` - AND, OR, NOT, XOR
- `test_current_sense.cpp` - CurrentSense
- `test_transformer.cpp` - Transformer
- `test_pid.cpp` - PID controller

### Regression Tests
Catch specific bugs from reappearing:
- `test_switch_regression.cpp`
- `test_hydraulic_loop_regression.cpp`
- `test_rug82_regression.cpp`

### Editor Tests
Test editor functionality:
- `test_commands.cpp` - Command execution
- `test_visual_node_widget.cpp` - Node rendering
- `test_blueprint_loading.cpp` - JSON loading

### UI Tests
Test UI framework:
- `test_ui_linear_layout.cpp`
- `test_ui_math.cpp`
- `test_ui_interning.cpp`

### Integration Tests
Test end-to-end scenarios:
- `test_simulation.cpp`
- `test_blueprint_loading.cpp`

### Blueprint V2 Tests
Test the new blueprint system:
- `test_blueprint.cpp` - Blueprint class
- `test_registry.cpp` - TypeRegistry
- `test_flattener.cpp` - Flattening
- `test_bake.cpp` - Bake/unbake
- `test_validation.cpp` - Validation

### Elaboration Parity Tests
Test JIT/AOT elaboration equivalence:
- `tests/blueprint_v2/test_codegen_export_parity.cpp` - Codegen vs JIT device/signal parity
- `tests/blueprint_v2/test_export_flattener_parity.cpp` - Flattener bridge resolution parity
- `tests/test_cross_path_signal_equivalence.cpp` - Cross-path signal allocation equivalence
- `tests/blueprint_v2/elaboration_parity_fixtures.h` - Shared test fixtures

### Codegen Tests
Test AOT generation:
- `test_codegen_sanitize.cpp`
- `test_aot_composite.cpp`
- `test_jit_aot_bridge_equivalence.cpp`

## Test Naming Convention

| Pattern | Example |
|---------|---------|
| `<Component>Test.<Scenario>_<Result>` | `ANDTest.BothTrue_ReturnsTrue` |
| `<Feature>Test.<Scenario>` | `LayoutTest.TooManyPorts_Wraps` |
| Regression | `SwitchRegression.CycleDetection_Works` |

## Test Helpers

### JSON Test Data
Embed test blueprints inline:
```cpp
TEST(BlueprintLoading, LoadsSimpleBattery) {
    const char* json = R"({
        "id": "test_battery",
        "cpp_class": true,
        "interface": [
            {"name": "v_out", "domain": 1, "direction": 1, "type": "V"}
        ]
    })";
    
    auto result = parse_json(json);
    EXPECT_TRUE(result.has_value());
}
```

### Simulation Helpers
```cpp
static void run_steps(SimulationState& st, std::vector<ComponentVariant>& comps, int n) {
    for (int i = 0; i < n; ++i) {
        st.clear_through();
        for (auto& c : comps) {
            std::visit([&](auto& comp) { comp.execute(st, 1.0f/60.0f); }, c);
        }
    }
}
```

## Adding New Tests

1. Create `tests/test_<feature>.cpp`
2. Add to `tests/CMakeLists.txt` using the `add_sim_test()` helper:
```cmake
add_sim_test(test_<feature>
    SOURCES tests/test_<feature>.cpp
    LINK_LIBS <additional_libs>
)
```
The helper handles: include dirs, gtest linking, `gtest_discover_tests()`, optional labels.
3. Run `cmake -B build` to regenerate

## Test File Index

| Category | Files |
|----------|-------|
| Components | `test_logic_gates.cpp`, `test_current_sense.cpp`, `test_transformer.cpp`, `test_pid.cpp`, `test_slew_rate.cpp`, `test_clamp_normalize.cpp` |
| Regression | `test_switch_regression.cpp`, `test_rug82_regression.cpp`, `test_hydraulic_accumulator_regression.cpp` |
| Editor | `test_commands.cpp`, `test_visual_*.cpp`, `test_blueprint_loading.cpp` |
| UI | `test_ui_*.cpp` |
| Blueprint V2 | `tests/blueprint_v2/test_*.cpp` |
| Elaboration Parity | `tests/blueprint_v2/test_codegen_export_parity.cpp`, `tests/blueprint_v2/test_export_flattener_parity.cpp`, `tests/test_cross_path_signal_equivalence.cpp` |
| Codegen | `test_aot_composite.cpp`, `test_codegen_sanitize.cpp`, `test_jit_aot_bridge_equivalence.cpp`, `test_lut_codegen.cpp` |
