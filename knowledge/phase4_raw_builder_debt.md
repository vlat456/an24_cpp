# Phase 4 Raw-Builder Debt Tracker

Purpose: make fallback/raw-builder usage explicit, measurable, and migratable.

Generated on: 2026-03-31

## Current Baseline

- Raw-builder callsites (`build_systems_dev(...)`) across `tests/*.cpp`: **107**
  (non-comment invocations only; verified 2026-03-31)
- Labeled CTest split (electrical-focused suites):
  - `production_path`: **24** tests
  - `raw_builder`: **10** tests

## Raw-Builder Usage by Test File (descending)

1. `tests/test_push_build_validation.cpp` — 26
2. `tests/test_electrical_primitives.cpp` — 16
3. `tests/test_electrical_island_build.cpp` — 13
4. `tests/test_electrical_handles_build.cpp` — 12
5. `tests/test_push_runtime_regression.cpp` — 12
6. `tests/test_electrical_parity_fixtures.cpp` — 6
7. `tests/editor_componentvariant_test.cpp` — 5
8. `tests/factory_validation_test.cpp` — 4
9. `tests/test_port_map_regression.cpp` — 4
10. `tests/test_production_path_parity.cpp` — 2
11. `tests/test_blueprint_integration.cpp` — 2
12. `tests/test_and_gate_debug.cpp` — 1
13. `tests/test_aot_composite.cpp` — 1
14. `tests/test_blueprint_loading.cpp` — 1
15. `tests/test_codegen_sanitize.cpp` — 1
16. `tests/test_jit_aot_bridge_equivalence.cpp` — 1

## Categorization

### Intentional Raw-Builder (keep)

These verify extraction/build internals directly and should remain raw-builder tests:

- `tests/test_electrical_island_build.cpp`
- `tests/test_electrical_handles_build.cpp`
- `tests/test_electrical_primitives.cpp` (builder-focused portions)
- `tests/test_push_build_validation.cpp` (builder validation rules)

### Mixed Label (needs split or dual label)

- `tests/test_electrical_parity_fixtures.cpp` — labeled `raw_builder` but contains
  both `ElectricalParityFixtures` (full JIT_Simulator path, production-like) and
  `ElectricalAotParity` (direct `build_systems_dev` calls). Consider splitting into
  separate executables or applying dual labels when CTest supports per-test labels.

### Migration Candidates (high value)

These should gain production-path counterparts (or migrate fully):

- `tests/test_electrical_parity_fixtures.cpp` (partially migrated via `test_production_path_parity.cpp`)
- `tests/test_push_runtime_regression.cpp`
- `tests/test_port_map_regression.cpp`
- `tests/test_blueprint_integration.cpp`
- `tests/factory_validation_test.cpp`

## Planned Migration Steps

1. Add production-path parity/validation variants for the top 3 migration-candidate files.
2. Label migrated suites as `production_path` and keep raw versions explicitly labeled `raw_builder`.
3. Track delta each patch:
   - raw-builder callsites count
   - production_path test count
4. Move at least 3 additional critical electrical scenarios from raw to production-path helpers.

## Progress Notes

- 2026-03-31: Added `tests/test_production_path_port_map.cpp` with 3 production-path
  regressions migrated from raw-builder-heavy `test_port_map_regression.cpp` patterns:
  - `ProductionPathPortMap.AndGateReadsWiredInputs`
  - `ProductionPathPortMap.NotGateReadsCorrectInput`
  - `ProductionPathPortMap.SubtractReadsBothInputs`
- Resulting label baseline moved from `production_path=18` to `production_path=22`.

- 2026-03-31: Added `tests/test_production_path_push_runtime.cpp` with 2 production-path
  runtime regressions migrated from raw-builder-heavy push runtime scenarios:
  - `ProductionPathPushRuntime.SinglePassSettlesLinearChain`
  - `ProductionPathPushRuntime.CycleRemainsFinite`
- Resulting label baseline moved from `production_path=22` to `production_path=24`.

## Exit Criteria for Phase 4 (operationalized)

- Raw-builder usage is explicit and intentionally scoped (this tracker maintained).
- Production-path electrical parity coverage includes:
  - single-island topology
  - multi-island topology
  - debug/diagnostics hook presence
- Measurable trend: production_path test count increases over baseline, raw-builder debt reduced or explicitly justified.
