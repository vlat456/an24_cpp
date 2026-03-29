# Phase 6 - Contract Consolidation & Cleanup TODO

## Scope
- Remove dead/legacy leftovers from RU19A and GS24 components
- Align blueprint defaults with C++ defaults for factory-parsed params
- Ensure strict factory param behavior is consistent (no silent param leakage)
- Add targeted regression tests for RU19A start/stop request behavior

## Success Criteria
- [x] No dead fields remain in RU19A or GS24 headers
- [x] Blueprint defaults match C++ defaults for all factory-parsed params
- [ ] All push-focused tests pass
- [ ] Full test suite passes

## Work Items

### RU19A Cleanup
- [x] Remove `runup_time` from RU19A component struct and verify no parser references remain
- [x] Remove `spinup_inertia` and `spindown_inertia` if truly unused in execute()
- [x] Verify `crank_time`, `ignition_time`, `start_timeout` are used correctly

### GS24 Cleanup
- [x] Compare GS24 defaults between `gs24.h`, factory parsing in `jit_solver.cpp`, and `library/systems/GS24.blueprint`
- [x] Remove any dead fields in GS24 if found

### Contract Alignment
- [x] Compare RU19A defaults between `ru19a.h`, factory parsing in `jit_solver.cpp`, and any blueprint files
- [x] Ensure all `consume_*_optional` calls have matching defaults in header
- [x] Verify strict param validation: no unknown params silently pass

### Testing
- [x] Add RU19A test case: `start()` request while OFF transitions to CRANKING on next commit
- [x] Add RU19A test case: `stop()` request while RUNNING transitions to STOPPING on next commit
- [ ] Run push-focused tests and full tests from correct build directories

## Verification Commands

### Build
```bash
cmake --build build -j8
```

### Push Runtime/Build Validation Tests
```bash
cd build/tests && ctest --output-on-failure -R "PushRuntime\.|PushBuildValidation\."
```

### Full Test Suite (build_fulltests)
```bash
cmake --build build_fulltests -j8
cd build_fulltests && ctest --output-on-failure
```

## Progress Log

- **2026-03-29**: Phase started. Initial scope defined. Beginning RU19A dead field removal (`runup_time`).
- **2026-03-29**: GS24 dead field cleanup: removed `start_time`, `k_motor`, `i_max_starter`, `i_max` from gs24.h (never used in execute/pre_load/commit). Removed corresponding `k_motor` factory parsing from jit_solver.cpp (param was consumed but never used in behavior). Blueprint defaults (r_internal, r_norton, target_rpm, v_nominal) already aligned with C++ defaults.
- **2026-03-29**: RU19A auto_start default sync: header defaults to `true`, factory was defaulting to `false`. Fixed factory to match header (`true`).
- **2026-03-29**: Added RU19A request semantics regression tests: `RU19AStartRequestTransitionsToCrankingNextFrame`, `RU19AStopRequestTransitionsToStoppingNextFrame`, `RU19AStartStopRequestsDoNotMutateCurrentFrameOutputs` in test_push_runtime_regression.cpp using direct component access via build_systems_dev.
- **2026-03-29**: Removed legacy `execution` metadata from all 79 blueprints in `library/**/*.blueprint`. Removed `parse_execution_phases()` and `validate_execution_domains_consistency()` functions from json_parser.cpp along with their call sites. Parser now accepts blueprints without execution blocks. `ExecutionPhases` struct and `DeviceInstance.execution` field retained for test usage only (tests construct ExecutionPhases directly without JSON parsing).
