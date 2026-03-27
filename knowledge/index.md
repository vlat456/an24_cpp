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
| Solver tuning | `knowledge/sor_optimization.md` | Practical SOR tuning guidance and safe defaults |
| SOR stabilization roadmap | `knowledge/sor_stabilization.md` | Scale-focused hardening plan for mixed-domain systems |
| Stable component design | `knowledge/component_authoring.md` | Rules for writing numerically stable components |
| Known issues / TODO | `knowledge/errors_TODO.md` | Architecture smells, bugs, and follow-up items |
| **SOR re-stamp fix** | `knowledge/15_solver_restamp_fix.md` | **ACTIVE: fix series-circuit convergence bug** |
| Scheduler refactor plan | `knowledge/13_scheduler_refactor_plan.md` | Detailed JIT/AOT plan for removing control-loop latency |
| Scheduler refactor epic | `knowledge/14_scheduler_refactor_epic.md` | Staged implementation checklist for coding agents |
| **Push migration plan** | `knowledge/16_push_migration_plan.md` | Migration from SOR to push propagation (game-grade sim) |

## Suggested Reading Order

### For general repo understanding

1. `knowledge/01_architecture.md`
2. `knowledge/10_quick_reference.md`
3. `knowledge/03_components.md`
4. `knowledge/04_blueprint_v2.md`

### For solver work

1. `knowledge/15_solver_restamp_fix.md` **(START HERE — active fix plan)**
2. `knowledge/02_simulation.md`
3. `knowledge/sor_optimization.md`
4. `knowledge/sor_stabilization.md`
5. `knowledge/component_authoring.md`
6. `knowledge/13_scheduler_refactor_plan.md`
7. `knowledge/16_push_migration_plan.md` **(ALTERNATIVE: Replace SOR with push propagation)**
8. `knowledge/errors_TODO.md`

### For editor work

1. `knowledge/05_editor.md`
2. `knowledge/09_ui_framework.md`
3. `knowledge/04_blueprint_v2.md`

### For adding new components

1. `knowledge/03_components.md`
2. `knowledge/component_authoring.md`
3. `knowledge/07_library.md`
4. `knowledge/08_testing.md`

## Fast Answers

| Question | Start Here |
|---------|------------|
| How does the solver work? | `knowledge/02_simulation.md` |
| How should I tune SOR? | `knowledge/sor_optimization.md` |
| How do we harden solver for scale? | `knowledge/sor_stabilization.md` |
| How do I write a stable component? | `knowledge/component_authoring.md` |
| How should runtime/codegen phases be refactored? | `knowledge/13_scheduler_refactor_plan.md` |
| What is the staged implementation roadmap? | `knowledge/14_scheduler_refactor_epic.md` |
| Where is the blueprint model? | `knowledge/04_blueprint_v2.md` |
| How is the editor structured? | `knowledge/05_editor.md` |
| What tests should I add? | `knowledge/08_testing.md` |
| What is already known to be problematic? | `knowledge/errors_TODO.md` |
