# Knowledge Base Index

Use this file as the entry point before crawling the rest of the repo.

## Core Notes

| Topic | File | Purpose |
|------|------|---------|
| Architecture overview | `knowledge/01_architecture.md` | High-level structure, major subsystems, key files |
| Simulation engine | `knowledge/02_simulation.md` | `SimulationState`, stamping, scheduling, solver flow |
| Component system | `knowledge/03_components.md` | Provider pattern, component categories, authoring shape |
| Blueprint V2 | `knowledge/04_blueprint_v2.md` | Immutable blueprint model, registry, flattening, validation |
| Visual editor | `knowledge/05_editor.md` | Document, scene, viewport, widgets, routing |
| AOT code generation | `knowledge/06_code_generation.md` | Generated systems, port registry, JIT vs AOT |
| Component library | `knowledge/07_library.md` | Blueprint JSON format, categories, special nodes |
| Testing | `knowledge/08_testing.md` | Test patterns, categories, helpers |
| UI framework | `knowledge/09_ui_framework.md` | Widgets, math, layout, rendering |
| Quick reference | `knowledge/10_quick_reference.md` | Build commands, paths, tuning defaults |

## Practical Notes

| Topic | File | Purpose |
|------|------|---------|
| Stable component design | `knowledge/component_authoring.md` | Rules for writing numerically stable components |
| Known issues / TODO | `knowledge/errors_TODO.md` | Architecture smells, bugs, and follow-up items |
| **Push improvement notes** | `knowledge/push_improvement.md` | Diagnostic analysis of closed electrical loop bugs and recommended electrical subsolver fix |
| **Push migration plan** | `knowledge/16_push_migration_plan.md` | Migration from legacy iterative solver to push propagation (game-grade sim) |
| Mixed-domain subsolver plan | `knowledge/18_subsolver_implementation_plan.md` | Concrete implementation plan for electrical-first local domain subsolvers |
| Subsolver step-by-step plan | `knowledge/19_subsolver_step_by_step_plan.md` | **Active implementation tracker** - detailed steps with completion status for phases 1-15 |
| Electrical migration release notes | `knowledge/20_electrical_migration_release_notes.md` | Final summary of implemented electrical subsolver migration and behavioral changes |
| AOT electrical refinement plan | `knowledge/21_aot_electrical_refinement_plan.md` | AOT-first plan to simplify runtime hot path and improve architectural elegance post-MVP |
| Phase 4 raw-builder debt | `knowledge/phase4_raw_builder_debt.md` | Explicit tracker of raw-builder test usage and migration candidates |
| Electrical component authoring | `knowledge/how_to_create_electrical_components.md` | Practical guide for creating primitive/wrapper electrical components and solver-role metadata |
| Blueprint zero-legacy cutover plan | `knowledge/blueprint_migration/zero_legacy_cutover_plan.md` | Strict no-fallback blueprint/runtime cutover checklist |
| Generated files policy | `knowledge/17_generated_files.md` | Which files are auto-generated and must not be edited manually |

## Suggested Reading Order

### For general repo understanding

1. `knowledge/01_architecture.md`
2. `knowledge/10_quick_reference.md`
3. `knowledge/03_components.md`
4. `knowledge/04_blueprint_v2.md`

### For solver work

1. `knowledge/02_simulation.md`
2. `knowledge/16_push_migration_plan.md` **(PRIMARY: Push propagation model)**
3. `knowledge/push_improvement.md` **(PRIMARY: Electrical subsolver motivation)**
4. `knowledge/18_subsolver_implementation_plan.md`
5. `knowledge/19_subsolver_step_by_step_plan.md`
6. `knowledge/component_authoring.md`
7. `knowledge/errors_TODO.md`

### For editor work

1. `knowledge/05_editor.md`
2. `knowledge/09_ui_framework.md`
3. `knowledge/04_blueprint_v2.md`

### For adding new components

1. `knowledge/03_components.md`
2. `knowledge/how_to_create_electrical_components.md`
3. `knowledge/component_authoring.md`
4. `knowledge/07_library.md`
5. `knowledge/08_testing.md`

## Fast Answers

| Question | Start Here |
|---------|------------|
| How does the solver work? | `knowledge/02_simulation.md` |
| How do I write a stable component? | `knowledge/component_authoring.md` |
| How do I add electrical components? | `knowledge/how_to_create_electrical_components.md` |
| Where is the blueprint model? | `knowledge/04_blueprint_v2.md` |
| How is the editor structured? | `knowledge/05_editor.md` |
| What tests should I add? | `knowledge/08_testing.md` |
| What is already known to be problematic? | `knowledge/errors_TODO.md` |
