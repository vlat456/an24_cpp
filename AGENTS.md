# An-24 Flight Simulation - Agent Guidelines

## Build System

### CMake Configuration
```bash
# Configure (Debug)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Configure (Release with optimizations)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all targets
cmake --build build -j$(nproc)

# Clean build
cmake --build build --target clean
```

### Running Tests
```bash
# Run all tests
cd build && ctest

# Run specific test executable
cd build/tests && ./editor_data_tests

# Run specific test with regex filter
ctest -R "editor_data" --output-on-failure

# Run single test suite from test executable
./tests/editor_data_tests --gtest_filter="DataTest.TestName"
```

### Key Targets
- `make` or `cmake --build build` - Build everything
- `ctest` - Run all tests
- `./build/examples/an24_editor` - Launch visual editor
- `./build/examples/benchmark_jit_vs_aot` - Run benchmarks

## Code Style Guidelines

### File Organization
- Use `#pragma once` as header guard (not `#ifndef` guards)
- Headers first (relative paths without angle brackets), then system headers

### Naming Conventions
| Context | Style | Examples |
|---------|-------|----------|
| Classes | PascalCase | `Battery`, `SimulationState`, `ComponentVariant` |
| Member variables | snake_case | `v_nominal`, `internal_r`, `dynamic_signals_count` |
| Functions | snake_case | `solve_electrical`, `allocate_signal`, `build_systems_dev` |
| Constants/Enums | PascalCase | `Domain::Electrical`, `PortNames::v_out` |
| Template params | PascalCase | `Provider`, `CompType` |
| Macros | UPPER_SNAKE_CASE | `PORTS`, `DOMAIN_MASK` |

### Types
- Use `float` for simulation values (voltage, current, temperature, RPM)
- Use `uint32_t` for signal indices and counts
- Use `std::string` for owned strings, `std::string_view` for references
- Use `std::vector<T>` for dynamic arrays
- Use `std::variant<Ts...>` for type-safe discriminated unions

### Formatting
- 4 space indentation (never tabs)
- Braces: opening brace on same line, closing brace on new line
- Struct/class members: public first (for POD types), then private
- Align similar declarations vertically
- Section dividers: `// ==...== Section Name ==...==`

### Documentation
- Use `///` for Doxygen comments above declarations
- Add file-level comment describing purpose
- Comment complex algorithms inline with `//`

### Component Development
Components must define:
- `static constexpr Domain domain` - Simulation domain (Electrical, Mechanical, etc.)
- `solve_*domain*()` method - Physics solver (optional per domain)
- `post_step()` method - State machine updates, optional
- `pre_load()` method - Initialization, optional
- `Provider provider` member - Port access via `provider.get(PortNames::port_name)`

### Simulation Domains
| Domain | Frequency | Method |
|--------|-----------|--------|
| Electrical | 60 Hz | `solve_electrical()` |
| Logical | 60 Hz | `solve_logical()` |
| Mechanical | 20 Hz | `solve_mechanical()` |
| Hydraulic | 5 Hz | `solve_hydraulic()` |
| Thermal | 1 Hz | `solve_thermal()` |

### Test Conventions
- Use Google Test macros: `TEST(TestSuite, TestName)`
- Place test JSON in inline raw string literals (`R"(...)"`)
- Use `parse_json()` to load configuration
- Initialize `SimulationState` with `allocate_signal()` for each signal
- Run simulation steps then `EXPECT_FLOAT_EQ()` for float comparisons

### Error Handling
- Use `std::optional<T>` for nullable return values
- Use assertions (`assert()`) for invariants in debug builds
- Avoid exceptions in simulation hot path

### Performance Notes
- Simulation runs at 60 Hz - keep per-step work minimal
- Use Structure of Arrays (SoA) for cache-friendly iteration
- Pre-allocate buffers, avoid dynamic allocation in solve loop

## Project Structure
```
src/
├── jit_solver/       # Runtime solver, components (template-based)
│   ├── components/   # All component implementations
│   ├── component.h   # Component base class interface
│   ├── state.h       # SimulationState (SoA arrays)
│   ├── jit_solver.h  # Build system, ComponentVariant
├── json_parser/      # JSON config parsing (DeviceInstance, Connection)
├── codegen/          # AOT code generation
├── editor/           # Visual blueprint editor (ImGui + OpenGL)
tests/                # Google Test executables (one per test suite)
examples/             # Demo programs (hello_world, benchmarks, editor)
library/              # Component library definitions (JSON schemas)
generated/            # AOT-generated C++ code
```

## Common Patterns

### Component Port Access (JIT)
```cpp
float v_bus = st.across[provider.get(PortNames::v_bus)];
st.across[provider.get(PortNames::rpm_out)] = rpm_value;
```

### Domain-Specific Update
```cpp
template <typename Provider>
void MyComponent<Provider>::solve_thermal(SimulationState& st, float dt) {
    float temp_in = st.across[provider.get(PortNames::temp_in)];
    st.across[provider.get(PortNames::temp_out)] = temp_out;
}
```

### State Machine in post_step()
```cpp
void MyComponent::post_step(SimulationState& st, float dt) {
    switch (state) {
        case OFF:
            if (st.across[provider.get(PortNames::control)] > threshold)
                state = RUNNING;
            break;
        case RUNNING:
            break;
    }
}
```

## AOT vs JIT Modes
- **JIT**: Components loaded dynamically from JSON, uses `ComponentVariant`
- **AOT**: Codegen generates C++ source with direct component calls for max performance
- Both share the same component templates and `Provider` pattern for port access
