# Quick Reference

## Build Commands

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build -j$(nproc)

# Run tests
cd build && ctest

# Run editor
./build/examples/an24_editor
```

## File Locations

| What | Where |
|------|-------|
| Simulation State | `src/jit_solver/state.h` |
| Component Base | `src/jit_solver/component.h` |
| Provider Pattern | `src/jit_solver/components/provider.h` |
| All Components | `src/jit_solver/components/all.h` |
| JIT Solver | `src/jit_solver/jit_solver.h` |
| Blueprint V2 | `src/blueprint_v2/blueprint/blueprint.h` |
| Type Registry | `src/blueprint_v2/registry/type_registry.h` |
| Flattener | `src/blueprint_v2/flattener/flattener.h` |
| Code Generator | `src/codegen/codegen.h` |
| Document | `src/editor/document.h` |
| Scene | `src/editor/visual/scene.h` |
| Port Registry | `src/jit_solver/components/port_registry.h` |
| Component Library | `library/**/*.blueprint` |
| Tests | `tests/*.cpp` |
| Generated Code | `generated/*.cpp, *.h` |

## Knowledge Notes

| Topic | File |
|------|------|
| Solver tuning | `knowledge/sor_optimization.md` |
| Stable component design | `knowledge/component_authoring.md` |
| Scheduler refactor plan | `knowledge/13_scheduler_refactor_plan.md` |
| Architecture overview | `knowledge/01_architecture.md` |

## Knowledge Notes

| Topic | File |
|------|------|
| Component internals | `knowledge/03_components.md` |
| Blueprint/library format | `knowledge/07_library.md` |
| Electrical-logical bridge nodes | `knowledge/11_domain_bridges.md` |
| `GSC.blueprint` bridge migration plan | `knowledge/12_gsc_bridge_plan.md` |

## Domain Values

| Domain | Value | Method | Frequency |
|--------|-------|--------|-----------|
| Electrical | 1 | `solve_electrical()` | 60 Hz |
| Logical | 2 | `solve_logical()` | 60 Hz |
| Mechanical | 4 | `solve_mechanical()` | 20 Hz |
| Hydraulic | 8 | `solve_hydraulic()` | 5 Hz |
| Thermal | 16 | `solve_thermal()` | 1 Hz |

## Port Directions

| Direction | Value | Meaning |
|-----------|-------|---------|
| Input | 0 | Data flows in |
| Output | 1 | Data flows out |
| Bidirectional | 2 | Either direction |

## Port Types

| Type | Symbol | Domain |
|------|--------|--------|
| Voltage | V | Electrical |
| Current | I | Electrical |
| Signal | Signal | Logical |
| Pressure | P | Hydraulic |
| Flow | Q | Hydraulic |
| Temperature | T | Thermal |
| Heat | H | Thermal |

## Component Template Pattern

```cpp
template <typename Provider = JitProvider>
class MyComponent {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;
    float param = 1.0f;
    
    void solve_electrical(SimulationState& st, float dt) {
        float in = st.across[provider.get(PortNames::v_in)];
        st.across[provider.get(PortNames::v_out)] = in * param;
    }
    
    void post_step(SimulationState& st, float dt) {}  // Optional
    void pre_load() {}  // Optional
};
```

## Simulation Loop

```cpp
for (int step = 0; step < total_steps; ++step) {
    st.clear_through();
    
    // 60 Hz
    for (auto& c : electrical) std::visit([&](auto& x) { x.solve_electrical(st, dt); }, c);
    for (auto& c : logical) std::visit([&](auto& x) { x.solve_logical(st, dt); }, c);
    
    // 20 Hz
    if (step % 3 == 0)
        for (auto& c : mechanical) std::visit([&](auto& x) { x.solve_mechanical(st, dt*3); }, c);
    
    // 5 Hz
    if (step % 12 == 0)
        for (auto& c : hydraulic) std::visit([&](auto& x) { x.solve_hydraulic(st, dt*12); }, c);
    
    // 1 Hz
    if (step % 60 == 0)
        for (auto& c : thermal) std::visit([&](auto& x) { x.solve_thermal(st, dt*60); }, c);
    
    st.precompute_inv_conductance();
    solve_sor_iteration(st.across.data(), st.through.data(),
                       st.inv_conductance.data(), st.dynamic_signals_count, omega);
    
    for (auto& c : all) std::visit([&](auto& x) { x.post_step(st, dt); }, c);
}
```

## Blueprint JSON Format

```json
{
  "version": "3.0",
  "id": "MyComponent",
  "cpp_class": true,
  "interface": [
    {"name": "v_in", "domain": 1, "direction": 0, "type": "V"},
    {"name": "v_out", "domain": 1, "direction": 1, "type": "V"}
  ],
  "param_defaults": {"gain": "1.0"}
}
```

## Test Pattern

```cpp
TEST(MyTest, Scenario_ExpectedResult) {
    auto comp = make_component();
    auto st = make_state();
    
    // Setup
    st.across[0] = 1.0f;
    
    // Execute
    comp.solve_electrical(st, 1.0f/60.0f);
    
    // Verify
    EXPECT_NEAR(st.across[1], expected, tolerance);
}
```

## Solver Tuning Defaults

For MSFS-style systems simulation, prefer stability over aggressive convergence.

Recommended starting point:

```cpp
constexpr float OMEGA = 1.3f;      // canonical project default
constexpr int INNER_SWEEPS = 1;    // keep 1 with current stamp-then-solve pipeline
```

Tuning guide:

| Situation | Change |
|---------|---------|
| Solver oscillates / spikes | Let adaptive runtime `omega_` reduce from `1.3` toward `1.0` |
| Persistent instability | Improve topology (dangling series nodes, near-shorts), then component params |
| Stiff measurement circuit | Lower measurement conductance defaults or add realistic series resistance |
| Need stronger damping globally | Consider lowering canonical `OMEGA` only with regression updates |

Rules of thumb:

- keep `INNER_SWEEPS = 1` unless solver is redesigned to re-stamp per inner iteration
- keep persistent state updates in `post_step()`, not in `solve_*()`
- validate dangling series devices and near-short source paths early
- do not replace electrical SOR with push propagation

## Centralized Runtime Config

Single source of truth for solver and JIT/editor warning knobs:

- `src/jit_solver/SOR_constants.h`

Namespaces in that file:

| Namespace | Purpose |
|---------|---------|
| `SOR` | Core relaxation constants (`OMEGA`, `INNER_SWEEPS`) |
| `SORAdaptive` | Adaptive runtime omega thresholds/scales |
| `JitElectricalWarnings` | JIT/editor electrical warning thresholds |
| `DomainSchedule` | Multi-domain execution periods |

Current high-current warning bands:

- `JitElectricalWarnings::HIGH_CURRENT_INFO_A = 300.0f`
- `JitElectricalWarnings::NEAR_SHORT_WARN_CURRENT_A = 1500.0f`

## Naming Conventions

| Context | Style | Example |
|---------|-------|---------|
| Classes | PascalCase | `Battery`, `SimulationState` |
| Functions | snake_case | `solve_electrical`, `allocate_signal` |
| Members | snake_case | `v_nominal`, `internal_r` |
| Constants | PascalCase | `Domain::Electrical` |
| Macros | UPPER_SNAKE | `PORTS`, `AOT_ALWAYS_INLINE` |

## Key Classes Quick Reference

| Class | Purpose |
|-------|---------|
| `SimulationState` | SoA arrays for physics |
| `JitProvider` | Runtime port lookup |
| `AotProvider` | Compile-time port lookup |
| `ComponentVariant` | Type-safe component union |
| `Blueprint` | Immutable circuit definition |
| `TypeRegistry` | Component type database |
| `Flattener` | Hierarchy → flat netlist |
| `EditorModel` | Undo/redo + dirty tracking |
| `Document` | Open file + simulator |
| `Scene` | Widget tree |
| `Viewport` | Pan/zoom transform |
| `InternedId` | Interned string handle |
