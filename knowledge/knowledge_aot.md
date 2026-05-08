# AOT Code Generation

Generates optimized C++ source files from blueprints for maximum runtime performance.

## NASA Standards Compliance & Refactoring

AOT code generator applies NASA C++ coding standards with strategic refactoring to reduce cyclomatic complexity and maintain 60-100 LOC per function.

### Refactoring Completed (Phase 1 & 2)

#### 1. electrical_codegen.cpp - extract_electrical_plan() ✅ REFACTORED (P1)

**Before**: 374 LOC, cyclomatic complexity 35-45  
**After**: 8 focused helpers + 54 LOC orchestrator

**Functions** (all using shared `build_algo` from `build_algorithms.h`):
- `AotExtractionAdapter` — Satisfies `ExtractionAdapter` concept (lenient: returns defaults, skips missing ports)
- `build_device_bindings()` — Wrapper component binding collection + dedup
- `build_component_debug()` — Debug metadata generation
- `AotPatchOpContext` — AOT adapter for `build_algo::build_patch_ops_generic`
- `extract_electrical_plan()` — Clean 4-phase orchestrator

**Extraction**: Uses shared `extract_with_table()` from `element_extraction.h` with `AotExtractionAdapter`. No per-kind if-chain.

**Complexity Reduction**: Cyclomatic complexity per function now 5-12 (was 35-45 monolithic)

#### 2. codegen.cpp - generate_source() ✅ REFACTORED (P2)

**Before**: 245 LOC, cyclomatic complexity 25-30  
**After**: 6 focused helpers + 70 LOC orchestrator

**New Functions** (all 28-55 LOC):
- `emit_source_prelude()` — 29 LOC, Includes, pragmas, template instantiations
- `parse_lut_table()` — 34 LOC, LUT parameter parsing (extracted from inline logic)
- `emit_constructor_params()` — 49 LOC, Parameter initialization + electrical plan setup
- `emit_preload_method()` — 45 LOC, pre_load() + LUT arena initialization
- `emit_solve_step_dispatch()` — 28 LOC, Computed goto dispatch table + MSVC fallback
- `emit_step_electrical_diagnostics()` — 28 LOC, Electrical solve + diagnostics logging
- `generate_source()` — 70 LOC, Clean 8-phase orchestrator

**Complexity Reduction**: Cyclomatic complexity per function now 3-8 (was 25-30 monolithic)

#### 3. codegen.cpp - generate_header() ✅ REFACTORED (P3)

**Before**: 500 LOC, cyclomatic complexity ~20-25  
**After**: 2 focused helpers + 182 LOC orchestrator

**New Functions** (all ~50-80 LOC):
- `emit_electrical_plan_debug()` — 52 LOC, Emit electrical island debug maps + max size computation
- `emit_systems_class_declaration()` — 80 LOC, Emit Systems class with device members, port indices, electrical bindings, methods
- `generate_header()` — 182 LOC, Clean 5-phase orchestrator (prelude → signals → electrical plan → global state → class declaration)

**Complexity Reduction**: Cyclomatic complexity per function now 5-8 (was 20-25 monolithic)

## Key Files

| File | Purpose |
|------|---------|
| `src/core/solvers/aot/codegen.h` | Main codegen API |
| `src/core/solvers/aot/electrical_codegen.cpp` | Electrical plan extraction |
| `src/core/solvers/aot/codegen_registry.cpp` | Component registry codegen |
| `src/core/solvers/common/build_algorithms.h` | Shared build algorithms |
| `src/core/solvers/common/element_extraction.h` | Shared extraction templates |
| `src/core/solvers/common/signal_union_rules.h` | Signal union rules |
| `src/blueprint_v2/elaboration/codegen_export.h` | Codegen elaboration export |
| `generated/*.cpp` / `generated/*.h` | AOT output (do not edit) |

## AOT vs JIT Comparison

| Aspect | JIT | AOT |
|--------|-----|-----|
| Port lookup | Runtime hash map (JitProvider) | Compile-time constexpr (AotProvider) |
| Component dispatch | `std::visit` or ErasedStep | Direct function calls |
| Build time | Near-instant | Requires C++ compilation |
| Use case | Editor, rapid iteration | Production, maximum performance |
| Electrical solve | Same `solve_nodal()` | Same `solve_nodal()` inlined |
