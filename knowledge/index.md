# Knowledge Base

| Topic | File | Purpose |
|-------|------|---------|
| Architecture overview | `knowledge/01_architecture.md` | High-level structure, subsystems |
| Simulation engine | `knowledge/02_simulation.md` | Simulator, SimulationState, pipeline, multi-domain solver |
| Component system | `knowledge/03_components.md` | Provider pattern, execute/commit API, ComponentRegistry |
| Blueprint model | `knowledge/04_blueprint_v2.md` | Immutable blueprint, nodes, wires, codec |
| Visual editor | `knowledge/05_editor.md` | Document, scene, widgets, WindowSystem |
| Component library | `knowledge/07_library.md` | Blueprint JSON format, all domains |
| Testing | `knowledge/08_testing.md` | Test patterns, helpers, full test inventory |
| Connectors / Providers | `knowledge/09_connectors.md` | SimVarProvider, SimConnect, connector nodes |
| Quick reference | `knowledge/10_quick_reference.md` | Build commands, paths, tuning |
| Component authoring | `knowledge/component_authoring.md` | Rules for stable components, design philosophy |
| How to create electrical | `knowledge/how_to_create_electrical_components.md` | Electrical components, solver roles |
| Persistence spec v1 | `knowledge/persistence_spec_v1.md` | Canonical blueprint JSON format contract |
| Persistence boundaries | `knowledge/persistence_boundaries.md` | Canonical vs session vs library vs legacy file roles |
| Persistence cutover status | `knowledge/errors_TODO.md` (Umbrella section) | Persistence-reset completion status mapped to GitHub issues #99-#103 |
| Known issues | `knowledge/errors_TODO.md` | Bugs, architectural debts |
| JIT/AOT shared algorithms | `knowledge/knowledge_jit.md` + `knowledge/knowledge_aot.md` | Build pipeline, island grouping, patch ops, extraction adapters |
| UI framework | `knowledge/11_ui_framework.md` | Scene/Widget/Grid subsystem (src/ui/) |
| SimConnect integration | `knowledge/12_simconnect.md` | MSFS 2024 V2 delta wire protocol |
| Multi-domain solver | `knowledge/13_multidomain_solver.md` | Electrical, hydraulic, pneumatic nodal solver |

## Reading Order

**General:** 01 → 10 → 03
**Solver:** 02 → 03 → component_authoring → how_to_create_electrical → 13
**Editor:** 05 → 04 → 11
**Adding components:** 03 → how_to_create_electrical → component_authoring → 07 → 08
**Build pipeline:** 02 → knowledge_jit → knowledge_aot → build_algorithms.h → element_extraction.h
**Connectors / External I/O:** 09_connectors → 02_simulation → simvar_provider.h
**UI Framework:** 11_ui_framework → 05_editor
