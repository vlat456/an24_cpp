# Knowledge Base

| Topic | File | Purpose |
|------|------|---------|
| Architecture overview | `01_architecture.md` | High-level structure, subsystems |
| Simulation engine | `02_simulation.md` | SimulationState, electrical subsolver, pipeline |
| Component system | `03_components.md` | Provider pattern, execute/commit API |
| Blueprint model | `04_blueprint_v2.md` | Immutable blueprint, nodes, wires |
| Visual editor | `05_editor.md` | Document, scene, widgets |
| Component library | `07_library.md` | Blueprint JSON format |
| Testing | `08_testing.md` | Test patterns, helpers |
| Quick reference | `10_quick_reference.md` | Build commands, paths, tuning |
| Component authoring | `component_authoring.md` | Rules for stable components |
| How to create electrical | `how_to_create_electrical_components.md` | Electrical components, solver roles |
| Known issues | `errors_TODO.md` | Bugs, architectural debts |

## Reading Order

**General:** 01 → 10 → 03 → 04  
**Solver:** 02 → component_authoring → how_to_create_electrical → errors_TODO  
**Editor:** 05 → 04  
**Adding components:** 03 → how_to_create_electrical → component_authoring → 07 → 08
