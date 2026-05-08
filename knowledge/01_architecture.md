# Architecture Overview

An-24 is a real-time flight simulation system built around a modular component architecture. It supports both JIT (Just-In-Time) and AOT (Ahead-Of-Time) execution modes using a unified Provider pattern.

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           Visual Editor                                  │
│  (ImGui + OpenGL, Blueprint editing, Node widgets, Wire routing)        │
├─────────────────────────────────────────────────────────────────────────┤
│                        Blueprint V2 Layer                                │
│  (Immutable data model, ComponentRegistry, Flattener, Validation,      │
│   Codec, EditorModel, LibraryIndex)                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                         UI Framework                                     │
│  (Scene, Widget, Grid, Layout, RenderContext — src/ui/)                 │
├─────────────────────────────────────────────────────────────────────────┤
│                         JIT Solver                                       │
│  (SimulationState, Domain scheduling, ComponentVariant dispatch,        │
│   Multi-domain nodal subsolver)                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                       Component Layer                                    │
│  (Template-based components with Provider pattern for port access)       │
├─────────────────────────────────────────────────────────────────────────┤
│                        Code Generation                                   │
│  (AOT C++ generation from blueprints, compile-time optimization)         │
├─────────────────────────────────────────────────────────────────────────┤
│                       SimConnect Bridge                                  │
│  (MSFS 2024 V2 delta wire protocol, SimVarProvider I/O)                  │
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
- `values[]` - node values used by both push scheduler and nodal subsolvers

### 4. Hybrid Execution Model
The simulator uses both push scheduling and domain-specific nodal subsolvers:
| Domain | Frequency | Execution |
|--------|-----------|-----------|
| Electrical | 60 Hz | Nodal island subsolver |
| Logical | 60 Hz | Push scheduler |
| Mechanical | 20 Hz | Push scheduler |
| Hydraulic | 5 Hz | Nodal island subsolver |
| Thermal | 1 Hz | Push scheduler |
| Pneumatic | 5 Hz | Nodal island subsolver |

### 5. ComponentVariant (Type-Safe Polymorphism)
Uses `std::variant` instead of virtual functions:
```cpp
using ComponentVariant = std::variant<
    Battery<JitProvider>,
    Switch<JitProvider>,
    AND<JitProvider>,
    ...
>;
```
Dispatched via `std::visit` or pre-built typed pointer lists (SolverOwnedRefs).

### 6. Multi-Domain Nodal Solver
A single domain-agnostic nodal solver (`solve_nodal`) handles:
- **Electrical**: voltage / current
- **Hydraulic**: pressure / flow
- **Pneumatic**: pressure / flow

Each domain has its own `NodalRuntimeState` pointer in `SimulationState`.

## Subsystem Map

| Subsystem | Path | Key Files |
|-----------|------|-----------|
| JIT Solver | `src/core/solvers/jit/` | `simulator.h`, `state.h`, `scheduler.h`, `jit_solver.h` |
| AOT Codegen | `src/core/solvers/aot/` | `codegen.h`, `electrical_codegen.cpp` |
| Shared Solver | `src/core/solvers/common/` | `build_algorithms.h`, `element_extraction.h`, `nodal_types.h` |
| Blueprint V2 | `src/blueprint_v2/` | `blueprint/blueprint.h`, `editor_model/editor_model.h`, `codec/blueprint_codec.h` |
| Editor | `src/editor/` | `window_system.h`, `document.h`, `visual/scene.h` |
| UI Framework | `src/ui/` | `core/scene.h`, `core/widget.h`, `core/grid.h` |
| SimConnect | `src/simconnect/` | `simconnect_provider.h`, `wire_protocol.h`, `wire_codec.h` |
| Component Registry | `src/core/model/` | `component_registry.h`, `component_spec.h` |
| JSON I/O | `src/io/json/` | `parse_json_api.h`, `component_registry_json_loader.h` |
| Domain Types | `src/core/` | `domain_types.h` |
| Strings | `src/core/strings/` | `interned_id.h` |
| Registry | `src/core/registry/` | `component_resolution.h` |

## File Reference

| Component | File |
|-----------|------|
| SimulationState | `src/core/solvers/jit/state.h` |
| Simulator | `src/core/solvers/jit/simulator.h` |
| PushScheduler | `src/core/solvers/jit/scheduler.h` |
| JIT Solver | `src/core/solvers/jit/jit_solver.h` |
| Nodal Subsolver | `src/core/solvers/jit/subsolvers/nodal_subsolver.h` |
| ComponentRegistry | `src/core/model/component_registry.h` |
| ComponentSpec | `src/core/model/component_spec.h` |
| Blueprint | `src/blueprint_v2/blueprint/blueprint.h` |
| EditorModel | `src/blueprint_v2/editor_model/editor_model.h` |
| BlueprintCodec | `src/blueprint_v2/codec/blueprint_codec.h` |
| Document | `src/editor/document.h` |
| WindowSystem | `src/editor/window_system.h` |
| Scene | `src/editor/visual/scene.h` |
| UI Scene | `src/ui/core/scene.h` |
| SimConnectProvider | `src/simconnect/simconnect_provider.h` |
