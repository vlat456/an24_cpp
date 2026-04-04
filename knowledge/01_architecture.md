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
SimulationState stores physics data in a unified signal array:
- `values[]` - node values used by both push scheduler and electrical subsolver

### 4. Hybrid Execution Model
The simulator uses both push scheduling and local electrical subsolver:
| Domain | Frequency | Execution |
|--------|-----------|-----------|
| Electrical | 60 Hz | Local island subsolver |
| Logical | 60 Hz | Push scheduler |
| Mechanical | 20 Hz | Push scheduler |
| Hydraulic | 5 Hz | Push scheduler |
| Thermal | 1 Hz | Push scheduler |

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

## Simulation Loop (Current)

```
For each frame (60 Hz):
  1. Electrical subsolver solves connected islands
  2. Push scheduler executes logical/mechanical/etc. components
  3. Commit pass for stateful components (Battery discharge, state transitions)
```

The electrical subsolver handles closed electrical networks while the push scheduler handles the rest.

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

## Related Knowledge Notes


- `knowledge/component_authoring.md` - rules for writing stable components
