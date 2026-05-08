# Component Authoring Guide

This guide is for writing components that behave well in the current solver.

Relevant code:

- `src/core/solvers/jit/components/all.h`
- `src/core/solvers/common/provider.h`
- `src/core/solvers/jit/state.h`
- `src/core/solvers/jit/build_factory.cpp` (AUTO-GENERATED — do not edit)
- `src/core/solvers/aot/codegen_registry.cpp` (codegen tool that produces the factory)
- `src/core/model/component_registry.h`
- `src/core/domain_types.h`

## Main rule

Write components so they are numerically boring.

That means:

- stamp conductance/current cleanly
- avoid hidden feedback inside `solve_*()`
- keep defaults moderate
- prefer stable approximation over perfect physical purity

## Component template pattern

Use the existing pattern:

```cpp
template <typename Provider = JitProvider>
class MyComponent {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;

    float param = 1.0f;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
```

## Rules

### 1. `execute()` should compute outputs, `commit()` should update state

Do not update persistent internal state in `execute()`. Use `commit()` for state transitions.

Bad:

```cpp
void execute(SimulationState& st, double dt) {
    integrator_state += error * dt;
    st.values[provider.get(PortNames::out)] = integrator_state;
}
```

Good:

```cpp
void execute(SimulationState& st, double /*dt*/) {
    st.values[provider.get(PortNames::out)] = integrator_state;
}

void commit(SimulationState& st, double dt) {
    integrator_state += error * dt;
}
```

Why:

- `execute()` runs during scheduler step
- state changes in `commit()` take effect next frame (one-frame delay semantics)
- this prevents combinatorial feedback loops

### 2. Electrical/hydraulic/pneumatic components participate in subsolver, not push

Components that model nodal elements (resistors, sources, valves, etc.) don't use `execute()` for stamping. Instead:

- They contribute elements to the **nodal build plan** at build time via `solver_role` metadata
- The **domain-agnostic nodal subsolver** solves node potentials for connected islands
- Components read solved values via `st.electrical_rt`, `st.hydraulic_rt`, or `st.pneumatic_rt` in `execute()` or `commit()`

See `knowledge/how_to_create_electrical_components.md` for details on solver roles.

### 3. State transitions go in `commit()`, not `execute()`

For stateful components (Relay, Switch, AZS, etc.):

- Read inputs in `execute()` — from previous frame's committed state
- Stage state changes in `execute()` or compute outputs
- Apply state changes in `commit()` — visible next frame

### 4. Avoid Divide where possible

Use `Clamp` + `Lerp` instead of division by signals that can hit zero.

### 5. Prefer ports over parameters

Anything that could be driven by another component should be a port, not a parameter.

### 6. Multi-domain components

Use `Domain` bitmask for components that touch multiple domains:

```cpp
static constexpr Domain domain = Domain::Electrical | Domain::Logical;
```

## Files

| File | Purpose |
|------|---------|
| `src/core/solvers/jit/components/all.h` | Include all components here |
| `src/core/solvers/common/provider.h` | Provider pattern |
| `src/core/solvers/jit/state.h` | SimulationState |
| `src/core/domain_types.h` | Domain, PortType enums |
| `src/core/model/component_registry.h` | ComponentRegistry |
| `src/core/solvers/jit/build_factory.cpp` | AUTO-GENERATED factory |
| `src/core/solvers/aot/codegen_registry.cpp` | Codegen registry tool |
