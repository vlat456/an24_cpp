# An-24 Flight Simulation - Agent Guidelines

## Knowledge Base

Before recrawling the repository, check:

- `knowledge/index.md` - entry point for project knowledge
- `knowledge/10_quick_reference.md` - fast paths and tuning defaults
- `knowledge/errors_TODO.md` - known issues and follow-up items
- `knowledge/component_authoring.md` - how to write stable components
- `knowledge/how_to_create_electrical_components.md` - electrical components and solver roles

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

## Code Style Guidelines

### File Organization

- Use `#pragma once` as header guard (not `#ifndef` guards)
- Headers first (relative paths without angle brackets), then system headers

### Naming Conventions

| Context          | Style            | Examples                                                   |
| ---------------- | ---------------- | ---------------------------------------------------------- |
| Classes          | PascalCase       | `Battery`, `SimulationState`, `ComponentVariant`           |
| Member variables | snake_case       | `v_nominal`, `internal_r`, `dynamic_signals_count`         |
| Functions        | snake_case       | `execute`, `commit`, `allocate_signal`, `build_systems_dev` |
| Constants/Enums  | PascalCase       | `Domain::Electrical`, `PortNames::v_out`                   |
| Template params  | PascalCase       | `Provider`, `CompType`                                     |
| Macros           | UPPER_SNAKE_CASE | `PORTS`, `DOMAIN_MASK`                                     |

### Types

- Use `float` for simulation values (voltage, current, temperature, RPM)
- Use `double` for time (`dt`) and accumulators (battery charge, integrator state)
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

Components define:

- `static constexpr Domain domain` - Simulation domain (Electrical, Mechanical, etc.)
- `Provider provider` member - Port access via `provider.get(PortNames::port_name)`
- `execute(SimulationState& st, double dt)` - Per-frame computation (required)
- `commit(SimulationState& st, double dt)` - State transitions, battery discharge (optional)
- `pre_load()` - Initialization (optional)

### Simulation Pipeline

The simulator uses a **hybrid model**:

1. **Clamp dt** - `dt = std::min(dt, MAX_DT)` where MAX_DT=0.1s to prevent physics explosions
2. **Pre-solve** - Update dynamic sources (CVS, variable conductance, AZS)
3. **Solve electrical** - Domain-agnostic nodal subsolver for electrical networks
4. **Solve hydraulic** - Domain-agnostic nodal subsolver for hydraulic networks (if present)
5. **Solve pneumatic** - Domain-agnostic nodal subsolver for pneumatic networks (if present)
6. **Push scheduler** - Execute all logical/mechanical/thermal components
7. **Commit pass** - Battery discharge, state transitions

**One-frame delay semantics**: State changes in `commit()` take effect in the next frame's `execute()`.

### Test Conventions

- Use Google Test macros: `TEST(TestSuite, TestName)`
- Place test JSON in inline raw string literals (`R"(...)"`)
- Use `parse_json()` to load configuration
- Initialize `SimulationState` with `allocate_signal()` for each signal
- Run simulation steps then `EXPECT_NEAR()` for float comparisons
- Use `make_device()` helper to avoid constructor ambiguity with empty ports `{}`

### Error Handling

- Use `std::optional<T>` for nullable return values
- Use assertions (`assert()`) for invariants in debug builds
- Avoid exceptions in simulation hot path

### Performance Notes

- Simulation runs at 60 Hz - keep per-step work minimal
- Use Structure of Arrays (SoA) - but current implementation uses flat `values[]` array
- Pre-allocate buffers, avoid dynamic allocation in solve loop

## Project Structure

```
src/
├── blueprint_v2/       # Blueprint model, codec, flattener, validation
│   ├── blueprint/      # Blueprint class
│   ├── codec/          # BlueprintCodec strict v1 persistence
│   ├── editor_model/   # EditorModel with undo/redo
│   ├── flattener/      # Blueprint flattening
│   ├── library/        # LibraryIndex
│   └── validation/     # Validation suite
├── core/
│   ├── model/          # ComponentRegistry, ComponentSpec
│   ├── registry/       # Component resolution
│   ├── solvers/
│   │   ├── aot/        # AOT code generation
│   │   ├── common/     # Shared solver utilities, nodal types
│   │   └── jit/        # JIT runtime, simulator, scheduler, components
│   ├── strings/        # String interning (InternedId)
│   └── utils/          # UnionFind, etc.
├── editor/             # Visual blueprint editor (ImGui + OpenGL)
│   ├── input/          # Canvas input FSM
│   ├── visual/         # Scene, widgets, rendering
│   └── window/         # Window management
├── io/json/            # JSON loading/parsing adapters
├── simconnect/         # MSFS 2024 SimConnect integration
└── ui/                 # UI framework (Scene, Widget, Grid)
tests/                  # Google Test executables
examples/               # Demo programs (editor, benchmarks)
library/                # Component library definitions (JSON blueprints)
generated/              # AOT-generated C++ code
```

## Common Patterns

### Component Port Access (JIT)

```cpp
float v_bus = st.values[provider.get(PortNames::v_bus)];
st.values[provider.get(PortNames::rpm_out)] = rpm_value;
```

### Execute + Commit Pattern

```cpp
void MyComponent::execute(SimulationState& st, double /*dt*/) {
    // Read inputs from committed state
    float in = st.values[provider.get(PortNames::v_in)];
    // Compute outputs
    st.values[provider.get(PortNames::v_out)] = in * gain;
}

void MyComponent::commit(SimulationState& st, double dt) {
    // Stage state change for next frame
    if (st.values[provider.get(PortNames::ctrl)] > threshold) {
        next_state = true;
    }
    state = next_state;
}
```

### Reading Solved Nodal State

```cpp
void MyComponent::execute(SimulationState& st, double /*dt*/) {
    if (st.electrical_rt != nullptr) {
        float current = get_branch_current(*st.electrical_rt, electrical_handle);
        st.values[provider.get(PortNames::i_out)] = current;
    }
    if (st.hydraulic_rt != nullptr) {
        float flow = get_branch_current(*st.hydraulic_rt, hydraulic_handle);
        st.values[provider.get(PortNames::flow_out)] = flow;
    }
}
```

## AOT vs JIT Modes

- **JIT**: Components loaded dynamically from JSON, uses `ComponentVariant`, runtime port lookup via `JitProvider`
- **AOT**: Codegen generates C++ with compile-time port resolution via `AotProvider` for maximum performance
- Both share the same component templates and `Provider` pattern
