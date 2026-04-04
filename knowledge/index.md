# Knowledge Base

| Topic | File | Purpose |
|-------|------|---------|
| Architecture overview | `knowledge/01_architecture.md` | High-level structure, subsystems |
| Simulation engine | `knowledge/02_simulation.md` | Simulator, SimulationState, pipeline |
| Component system | `knowledge/03_components.md` | Provider pattern, execute/commit API |
| Blueprint model | `knowledge/04_blueprint_v2.md` | Immutable blueprint, nodes, wires |
| Visual editor | `knowledge/05_editor.md` | Document, scene, widgets |
| AOT codegen | `knowledge_aot.md` | C++ code generation, signal allocation |
| JIT solver | `knowledge_jit.md` | Runtime component loading, scheduler |
| Component library | `knowledge/07_library.md` | Blueprint JSON format |
| Testing | `knowledge/08_testing.md` | Test patterns, helpers |
| Quick reference | `knowledge/10_quick_reference.md` | Build commands, paths, tuning |
| Component authoring | `knowledge/component_authoring.md` | Rules for stable components, design philosophy (ports-over-params, minimize C++, avoid Divide) |
| How to create electrical | `knowledge/how_to_create_electrical_components.md` | Electrical components, solver roles |
| Known issues | `knowledge/errors_TODO.md` | Bugs, architectural debts |

## Reading Order

**General:** 01 → 10 → 03  
**Solver:** 02 → knowledge_jit → knowledge_aot → component_authoring → how_to_create_electrical  
**Editor:** 05 → 04  
**Adding components:** 03 → how_to_create_electrical → component_authoring → 07 → 08
