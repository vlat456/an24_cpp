# Architecture Overview

An-24 is a real-time flight simulation system built around a modular component architecture. It supports both JIT (Just-In-Time) and AOT (Ahead-Of-Time) execution modes using a unified Provider pattern.

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           Visual Editor                                  │
│  (ImGui + OpenGL, Blueprint editing, Node widgets, Wire routing)        │
├─────────────────────────────────────────────────────────────────────────┤
│                        Blueprint V2 Layer                                │
│  (Immutable data model, TypeRegistry, Flattener, Validation, Commands)  │
├─────────────────────────────────────────────────────────────────────────┤
│                         JIT Solver                                       │
│  (SimulationState SoA, Domain scheduling, ComponentVariant dispatch)    │
├─────────────────────────────────────────────────────────────────────────┤
│                       Component Layer                                    │
│  (Template-based components with Provider pattern for port access)       │
├─────────────────────────────────────────────────────────────────────────┤
│                        Code Generation                                   │
│  (AOT C++ generation from blueprints, compile-time optimization)         │
└─────────────────────────────────────────────────────────────────────────┘
```

## Key Design Decisions

### 1. Provider Pattern (Zero-Overhead Abstraction)
Same component templates work with both:
- **JitProvider**: Runtime hash map lookup (flexible, used in editor)
- **AotProvider**: Compile-time constexpr resolution (maximum performance)

### 2. Immutable Blueprint Model
Blueprint V2 uses copy-on-write semantics:
- All mutations return new Blueprint instances
- Enables cheap snapshots for undo/redo
- Thread-safe by design

### 3. Structure of Arrays (SoA)
SimulationState stores physics data in separate arrays:
- `across[]` - potentials (voltage, pressure, temperature)
- `through[]` - flows (current, flow rate, heat flux)
- `conductance[]` - accumulated conductances for SOR solver

### 4. Domain-Based Scheduling
Components run at different frequencies based on physics domain:
| Domain | Frequency | Method |
|--------|-----------|--------|
| Electrical | 60 Hz | `solve_electrical()` |
| Logical | 60 Hz | `solve_logical()` |
| Mechanical | 20 Hz | `solve_mechanical()` |
| Hydraulic | 5 Hz | `solve_hydraulic()` |
| Thermal | 1 Hz | `solve_thermal()` |

### 5. ComponentVariant (Type-Safe Polymorphism)
Uses `std::variant` instead of virtual functions:
```cpp
using ComponentVariant = std::variant<
    Battery<JitProvider>,
    Switch<JitProvider>,
    AND<JitProvider>,
    // ... all components
>;
```

## Directory Structure

```
src/
├── jit_solver/           # Runtime solver + components
│   ├── components/       # All component implementations
│   ├── state.h           # SimulationState (SoA arrays)
│   ├── jit_solver.h      # Build system, ComponentVariant
│   └── scheduling.h      # Domain-based scheduling
├── blueprint_v2/         # Modern blueprint data model
│   ├── blueprint/        # Core Blueprint class
│   ├── registry/         # TypeRegistry
│   ├── flattener/        # Blueprint flattening
│   ├── interface/        # Port descriptors
│   └── validation/       # Invariant checking
├── editor/               # Visual blueprint editor
│   ├── data/             # Legacy data structures
│   ├── visual/           # Widgets, rendering
│   ├── commands/         # Command pattern
│   └── router/           # Wire routing
├── codegen/              # AOT code generation
├── json_parser/          # JSON config parsing
└── ui/                   # Generic UI framework

library/                  # Component definitions (JSON)
tests/                    # Google Test suites
examples/                 # Demo programs
generated/                # AOT-generated C++ code
```

## Simulation Loop

```
For each frame (60 Hz):
  1. Clear through[] and conductance[]
  2. For each electrical component: solve_electrical()
  3. For each logical component: solve_logical()
  4. If step % 3 == 0: solve_mechanical() (20 Hz)
  5. If step % 12 == 0: solve_hydraulic() (5 Hz)
  6. If step % 60 == 0: solve_thermal() (1 Hz)
  7. SOR iteration: across += through * inv_conductance * omega
  8. For each component: post_step()
```

## Key Files Quick Reference

| Purpose | File |
|---------|------|
| Simulation State | `src/jit_solver/state.h` |
| Component Base | `src/jit_solver/component.h` |
| Provider Pattern | `src/jit_solver/components/provider.h` |
| All Components | `src/jit_solver/components/all.h` |
| Blueprint V2 | `src/blueprint_v2/blueprint/blueprint.h` |
| Type Registry | `src/blueprint_v2/registry/type_registry.h` |
| Code Generator | `src/codegen/codegen.h` |
| JSON Parser | `src/json_parser/json_parser.h` |
