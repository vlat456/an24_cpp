# Phase 6 - Contract Consolidation & Cleanup TODO

## Scope
- Remove dead/legacy leftovers from RU19A and GS24 components
- Align blueprint defaults with C++ defaults for factory-parsed params
- Ensure strict factory param behavior is consistent (no silent param leakage)
- Add targeted regression tests for RU19A start/stop request behavior

## Success Criteria
- [ ] No dead fields remain in RU19A or GS24 headers
- [ ] Blueprint defaults match C++ defaults for all factory-parsed params
- [ ] All push-focused tests pass
- [ ] Full test suite passes

## Work Items

### RU19A Cleanup
- [ ] Remove `runup_time` from RU19A component struct and verify no parser references remain
- [ ] Remove `spinup_inertia` and `spindown_inertia` if truly unused in execute()
- [ ] Verify `crank_time`, `ignition_time`, `start_timeout` are used correctly

### GS24 Cleanup
- [ ] Compare GS24 defaults between `gs24.h`, factory parsing in `jit_solver.cpp`, and `library/systems/GS24.blueprint`
- [ ] Remove any dead fields in GS24 if found

### Contract Alignment
- [ ] Compare RU19A defaults between `ru19a.h`, factory parsing in `jit_solver.cpp`, and any blueprint files
- [ ] Ensure all `consume_*_optional` calls have matching defaults in header
- [ ] Verify strict param validation: no unknown params silently pass

### Testing
- [ ] Add RU19A test case: `start()` request while OFF transitions to CRANKING on next commit
- [ ] Add RU19A test case: `stop()` request while RUNNING transitions to STOPPING on next commit
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
