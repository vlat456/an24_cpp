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
├── core/                    # Core simulation engine
│   ├── model/               # ComponentSpec, ComponentRegistry, TypePresentation
│   ├── solvers/
│   │   ├── jit/             # Runtime solver + components
│   │   │   ├── jit_solver.h # Build system, ComponentVariant
│   │   │   ├── jit_solver.cpp (1958 LOC - needs splitting)
│   │   │   ├── scheduler.h  # PushScheduler
│   │   │   ├── simulator.h # Simulator class
│   │   │   ├── state.h      # SimulationState (SoA arrays)
│   │   │   ├── provider.h   # Provider pattern (port access abstraction)
│   │   │   ├── components/  # All component implementations (~70)
│   │   │   └── subsolvers/  # Electrical subsolver
│   │   ├── aot/             # AOT code generation
│   │   │   ├── codegen.h
│   │   │   ├── codegen_composite.cpp  # Composite codegen (Flattener pipeline)
│   │   │   ├── codegen_header.cpp
│   │   │   ├── codegen_source.cpp
│   │   │   └── electrical_codegen.cpp
│   │   └── common/          # Shared types (signal_allocation, signal_union_rules, port_registry)
│   ├── registry/            # Component resolution (DELETED: composite_expansion)
│   └── utils/               # UnionFind, shared utilities
├── blueprint_v2/            # Modern blueprint data model
│   ├── blueprint/           # Core Blueprint class
│   ├── library/             # BlueprintLibrary, type_def_to_blueprint
│   ├── flattener/           # Blueprint flattening + FlatNetlist
│   ├── elaboration/         # FlatNetlist → BuildInput conversion
│   │   ├── elaboration_utils.h/.cpp    # Lightweight shared utils (no JIT dep)
│   │   ├── elaboration_detail.h        # Shared device builder (JIT+codegen)
│   │   ├── codegen_export.h/.cpp       # CodegenBuildInput + elaborate_for_codegen
│   │   └── sim_export.h/.cpp           # JitBuildInput + elaborate_for_jit
│   ├── interface/           # Port descriptors
│   └── validation/          # Invariant checking
├── editor/                  # Visual blueprint editor
│   ├── data/                # Legacy data structures
│   ├── visual/              # Widgets, rendering
│   ├── commands/            # Command pattern
│   └── router/              # Wire routing
├── io/json/                 # JSON loading/parsing adapters
└── ui/                      # Generic UI framework

library/                     # Component definitions (JSON)
tests/                       # Google Test suites
examples/                    # Demo programs
generated/                   # AOT-generated C++ code
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
| Simulation State | `src/core/solvers/jit/state.h` |
| Component Provider | `src/core/solvers/common/provider.h` |
| Provider Pattern | `src/core/solvers/common/provider.h` |
| All Components | `src/core/solvers/jit/components/all.h` |
| JIT Solver Build | `src/core/solvers/jit/jit_solver.h` |
| Push Scheduler | `src/core/solvers/jit/scheduler.h` |
| Simulator | `src/core/simulator.h` |
| Blueprint V2 | `src/blueprint_v2/blueprint/blueprint.h` |
| Canonical Component Registry | `src/core/model/component_registry.h` |
| Code Generator | `src/core/solvers/aot/codegen.h` |
| JSON Parser | `src/io/json/parse_json_api.h` |

## Related Knowledge Notes

- `knowledge/component_authoring.md` - rules for writing stable components
- `knowledge/10_quick_reference.md` - updated with new file paths
