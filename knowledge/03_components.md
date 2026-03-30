# Component System

This document reflects the current component model after electrical subsolver migration.

## Component Shape

Components are template classes parameterized by provider:

```cpp
template <typename Provider = JitProvider>
class MyComponent {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load();
};
```

Important:

- runtime hook names are `execute` and `commit` (not `solve_*` / `finalize_step`)
- `commit` now always receives `dt`

---

## Provider Pattern

### JitProvider (runtime)

```cpp
struct JitProvider {
    uint32_t get(PortNames p) const;
    bool has(PortNames p) const;
    void set(PortNames p, uint32_t idx);
};
```

### AotProvider (compile-time)

```cpp
template <PortNames P, uint32_t Idx>
struct Binding { ... };

template <typename... Bindings>
struct AotProvider {
    static constexpr uint32_t get(PortNames p);
};
```

---

## ComponentVariant

All JIT components are stored in a `std::variant` (`ComponentVariant`) and dispatched via `std::visit`.

Files:

- `src/jit_solver/components/all.h`
- `src/jit_solver/jit_solver.h`

---

## Electrical Component Categories (Current)

### Solver-owned propagators

These provide electrical elements to the subsolver; they do not push-write electrical propagation:

- wrappers: `Battery`, `Generator`, `Resistor`
- primitives: `ElectricalSource`, `ElectricalConductance`

### Fixed reference

- `RefNode` clamps node potential (also represented as solver fixed node)

### Observers/derived outputs

- `IndicatorLight`: brightness from solved `v_in`
- `CurrentSense`: `i_out` from solved branch current handle

### Stub/unsupported electrical load

- `HighPowerLoad` is currently a no-op stub and does not affect circuit solve

---

## Primitive-First Direction

Two first-class electrical primitives exist now:

- `ElectricalSource` -> `TheveninSource`
- `ElectricalConductance` -> `ConductanceBranch`

These are solver-owned and intended as base building blocks.

Wrapper components remain for compatibility/authoring convenience, with planned decomposition over time.

---

## Metadata-Driven Solver Roles

Type definitions can declare optional `solver_role` metadata.

Current supported role kinds:

- `FixedVoltageNode`
- `TheveninSource`
- `ConductanceBranch`

Role metadata defines:

- `kind`
- `ports` mapping role key -> component port name
- `params` mapping role key -> param name

Examples exist in:

- `library/electrical/ElectricalSource.blueprint`
- `library/electrical/ElectricalConductance.blueprint`
- `library/RefNode.blueprint`

If `solver_role` is absent, builder may use explicit classname fallback extraction.

---

## Adding New Electrical Components

Use one of two paths:

1. **Preferred:** create a primitive with `solver_role` metadata
2. **Transitional:** wrapper component with explicit classname extraction path

For practical instructions, see:

- `knowledge/how_to_create_electrical_components.md`
