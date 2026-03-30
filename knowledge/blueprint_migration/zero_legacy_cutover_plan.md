# Zero-Legacy Blueprint Cutover Plan

> **Branch:** `push_migration`
> **Created:** 2026-03-30
> **Status:** ACTIONABLE — ready for execution

---

## 1. Objective

Achieve a strict zero-legacy codebase where:

- Blueprint v2 (version 3.0 format) is the **only** accepted input model.
- **No** fallback paths, compatibility shims, converter layers, or dual-read logic exist in production code.
- Legacy or malformed inputs **fail fast** at parse/load/build time with actionable error messages.
- Runtime hot paths (JIT execute loop, AOT step functions) contain **zero** validation branches — all validation happens at load/build boundaries.

### Invariants (Non-Negotiable)

| ID | Invariant |
|----|-----------|
| I-1 | `load_type_registry()` rejects any `.blueprint` file without `"version": "3.0"` — **hard throw** |
| I-2 | `build_systems_dev()` rejects any `DeviceInstance` whose `classname` is unknown — **hard throw** |
| I-3 | `build_systems_dev()` rejects any unconsumed parameter — **hard throw** (no silent whitelist bypass) |
| I-4 | `bp2::decode()` rejects any blueprint without `"version": "3.0"` — **error return** |
| I-5 | No `known_library_unused_params()` whitelist exists in production code |
| I-6 | No `is_migrated_component_class()` gate exists — all component classes are migrated by definition |
| I-7 | No `observe_electrical()` shim exists on any component |
| I-8 | `Simulator::get_wire_voltage()` has no `:*.ext` fallback port name rewriting |
| I-9 | `PushScheduler::step()` is the single execution entry point — no domain-specific solve methods in public API |
| I-10 | Every library `.blueprint` has strict, explicit `domains`, `interface`, `cpp_class`, and `param_defaults` — no inference |

---

## 2. Scope Boundaries

### In Scope

| Area | What |
|------|------|
| JIT factory | `src/jit_solver/jit_solver.cpp` — remove compatibility whitelist, migration gates |
| JIT simulator | `src/jit_solver/simulator.cpp` — remove port name fallback rewriting |
| Component headers | `src/jit_solver/components/voltage_sense.h` — remove `observe_electrical()` shim |
| Component source | `src/jit_solver/components/voltage_sense.cpp` — remove `observe_electrical()` |
| Parser/loader | `src/json_parser/json_parser.cpp` — audit for any remaining inference or tolerance |
| Blueprint codec | `src/blueprint_v2/codec/blueprint_codec.cpp` — ensure strict-only decode |
| Library blueprints | `library/**/*.blueprint` — normalize metadata, remove stale param_defaults |
| Tests | `tests/` — add rejection tests, remove any test that relies on legacy paths |
| Knowledge docs | `knowledge/` — update references to removed legacy paths |
| AOT codegen | `src/codegen/codegen.cpp` — verify no fallback port resolution |

### Out of Scope

| Area | Why |
|------|-----|
| Editor data model (`src/editor/data/blueprint.h`) | Removed in zero-legacy cutover; use `src/blueprint_v2/blueprint/blueprint.h` and `src/editor/data/node_content.h`. |
| UI framework (`src/ui/`, `src/editor/visual/`) | UI layout fallbacks (`get_size(fallback)`, `fallback_lane_y`) are UI layout concerns, not simulation legacy. |
| Router fallback (`src/editor/router/router.h` L-shape fallback) | Header removed in cutover; routing uses active visual/router code paths only. |
| Port type compatibility checks | `are_ports_compatible()` is active validation logic, not a legacy shim. |
| `parse_number.h` strtod fallback | Numeric parsing concern, not blueprint model legacy. |
| MSVC computed-goto switch fallback in codegen | Compiler portability concern, not data model legacy. |
| GS24/RU19A test-pad fixtures | These are test harnesses; real systems use base nodes. Keep as-is unless they reference removed APIs. |

---

## 3. Target Files and Touch Points

### Primary Files (Must Change)

| File | What to Do | Risk |
|------|------------|------|
| `src/jit_solver/jit_solver.cpp:87-141` | Remove `is_migrated_component_class()` gate and `known_library_unused_params()` whitelist entirely | Medium — must verify all 60+ component branches still get entered |
| `src/jit_solver/jit_solver.cpp:273-276` | Remove `if (!is_migrated)` skip guard | Low — dead code after gate removal |
| `src/jit_solver/jit_solver.cpp:286-289` | Remove `known_unused_params` fetch and comment about backward compatibility | Low |
| `src/jit_solver/jit_solver.cpp:336-340` | Remove silent whitelist bypass in `validate_all_params_consumed` | Medium — `inv_internal_r`, `inv_capacity`, `port_edge`, `exposed_direction`, `exposed_type`, `resistance` must be removed from library `.blueprint` files first |
| `src/jit_solver/simulator.cpp:122-132` | Remove `:*.ext` port name fallback rewriting in `get_wire_voltage()` | Low — verify no callers depend on this |
| `src/jit_solver/components/voltage_sense.h:22` | Remove `observe_electrical()` declaration | Low |
| `src/jit_solver/components/voltage_sense.cpp:5-10` | Remove `observe_electrical()` implementation | Low |
| `library/electrical/Battery.blueprint` | Remove `inv_internal_r`, `inv_capacity` from `param_defaults` | Low |
| `library/electrical/Generator.blueprint` | Remove `inv_internal_r` from `param_defaults` (if present) | Low |
| `library/Bus.blueprint` | Remove `port_edge` from `param_defaults` (if present) | Low |
| `library/BlueprintInput.blueprint` | Remove `exposed_direction`, `exposed_type` from `param_defaults` (if present) | Low |
| `library/electrical/Load.blueprint` | Remove `resistance` from `param_defaults` (if present) | Low |

### Secondary Files (May Change)

| File | What to Do |
|------|------------|
| `src/jit_solver/jit_solver.cpp:1410-1418` | Audit cycle-fallback warning — this is a valid topological sort fallback for cycles, NOT a legacy shim. **Keep.** |
| `src/jit_solver/jit_solver.cpp:721-739` | Audit TimeDelay `"delay"` param shorthand — this is a valid convenience API. Decide: keep as explicit feature or force `delay_on`/`delay_off`. |
| `src/codegen/codegen.cpp:106` | Already strict (NO FALLBACKS, fail hard). Verify comment is accurate. |
| `knowledge/10_quick_reference.md` | Update to remove legacy domain table language |
| `knowledge/errors_TODO.md` | Close Issue #14 (Zero-Fallback Metadata Cutover) |
| `knowledge/16_push_migration_plan.md` | Mark as COMPLETED |

---

## 4. Phase-by-Phase Migration Steps

### Phase A: Library Blueprint Normalization

**Goal:** Make all `library/**/*.blueprint` files strict-clean so that removing the whitelist doesn't break loading.

**Entry Criteria:**
- Current `build/` builds and all push-migration tests pass
- All `.blueprint` files load successfully with current code

**Actions:**
1. Audit every `.blueprint` file for params in `known_library_unused_params()`:
   - `inv_internal_r` — remove from Battery.blueprint, Generator.blueprint `param_defaults`
   - `inv_capacity` — remove from Battery.blueprint `param_defaults`
   - `port_edge` — remove from Bus.blueprint `param_defaults` (if present)
   - `exposed_direction` — remove from BlueprintInput.blueprint `param_defaults` (if present)
   - `exposed_type` — remove from BlueprintInput.blueprint `param_defaults` (if present)
   - `resistance` — remove from Load.blueprint `param_defaults` (if present)
2. For each removed param: verify no component's `consume_*` call reads it
3. Build and run tests

**Exit Criteria:**
- `cmake --build build -j8` succeeds
- `ctest --output-on-failure -R "PushRuntime\.|PushBuildValidation\."` from `build/tests` — all pass
- `cmake --build build_fulltests -j8 && ctest --output-on-failure` from `build_fulltests` — all pass
- No `.blueprint` file contains any key from the former whitelist

**Commit:** `chore: remove computed/unused param_defaults from library blueprints`

---

### Phase B: Remove Compatibility Whitelist

**Goal:** Delete `known_library_unused_params()` and its consumption. After Phase A, no `.blueprint` file should contain those keys, so the whitelist is dead code.

**Entry Criteria:**
- Phase A commit is green
- grep confirms zero hits for whitelist keys across `library/`

**Actions:**
1. Delete `known_library_unused_params()` function (jit_solver.cpp:131-141)
2. Remove `const auto& known_unused_params = known_library_unused_params();` (line ~289)
3. Remove the `known_unused_params.find(key)` branch inside `validate_all_params_consumed` (lines ~336-340)
4. Remove the `// to maintain backward compatibility with existing blueprints` comment (line ~288)
5. Build and run tests

**Exit Criteria:**
- `cmake --build build -j8` succeeds
- `ctest --output-on-failure -R "PushRuntime\.|PushBuildValidation\."` from `build/tests` — all pass
- `grep -rn "known_library_unused\|known_unused_params\|backward.compat" src/` returns zero hits

**Commit:** `refactor: remove known_library_unused_params compatibility whitelist`

---

### Phase C: Remove Migration Gate

**Goal:** Delete `is_migrated_component_class()` — all components are migrated by definition. The function currently gates entry into `build_systems_dev` component creation; removing it means unknown classnames will fall through to the end of the `if/else if` chain and should produce a clear error.

**Entry Criteria:**
- Phase B commit is green

**Actions:**
1. Delete `is_migrated_component_class()` function (jit_solver.cpp:92-129)
2. Delete `is_source_component_class()` function (jit_solver.cpp:88-90) — or keep if used elsewhere (check call sites)
3. Remove `bool is_migrated = is_migrated_component_class(dev.classname);` (line ~273)
4. Remove `if (!is_migrated) { continue; }` guard (lines ~275-277)
5. Add an explicit final `else` clause at the end of the component creation chain that throws:
   ```cpp
   else {
       throw std::runtime_error("Unknown component class '" + dev.classname +
           "' for device '" + dev.name + "'. No factory handler registered.");
   }
   ```
6. Verify `is_source_component_class()` is still needed for scheduler source/consumer classification (it is — keep it)
7. Build and run tests

**Exit Criteria:**
- `cmake --build build -j8` succeeds
- Full test pass
- `grep -rn "is_migrated" src/` returns zero hits
- Add a new test `PushBuildValidation.UnknownClassnameThrows` that verifies unknown classnames throw

**Commit:** `refactor: remove is_migrated_component_class gate, add unknown-class hard fail`

---

### Phase D: Remove Shim Methods

**Goal:** Remove `observe_electrical()` from VoltageSense. This is a legacy scheduling hook that duplicates `execute()`.

**Entry Criteria:**
- Phase C commit is green

**Actions:**
1. Remove `observe_electrical()` declaration from `voltage_sense.h` (line 22)
2. Remove `observe_electrical()` implementation from `voltage_sense.cpp` (lines 5-10)
3. Search for any call sites: `grep -rn "observe_electrical" src/ tests/`
4. If call sites exist in scheduler or tests, update them to use `execute()`
5. Verify the `// Stage 2 hook shim` comment is removed
6. Build and run tests

**Exit Criteria:**
- `cmake --build build -j8` succeeds
- `grep -rn "observe_electrical\|hook.shim" src/` returns zero hits
- All tests pass

**Commit:** `refactor: remove VoltageSense::observe_electrical shim`

---

### Phase E: Remove Port Name Fallback Rewriting

**Goal:** Remove the `":*.ext"` port name fallback in `Simulator::get_wire_voltage()`.

**Entry Criteria:**
- Phase D commit is green

**Actions:**
1. Examine `simulator.cpp:122-132`:
   ```cpp
   // Current: tries "device.port", then rewrites to "device:port.ext"
   std::string fallback = port_name.substr(0, dot) + ":" + port_name.substr(dot + 1) + ".ext";
   ```
2. Remove the fallback rewrite block (lines 122-132), keeping only the direct lookup
3. The function should now be:
   ```cpp
   auto it = build_result_->port_to_signal.find(port_name);
   if (it == build_result_->port_to_signal.end()) {
       return 0.0f;
   }
   ```
4. Search tests for any that pass port names relying on `:*.ext` rewriting
5. Build and run full test suite

**Exit Criteria:**
- `cmake --build build -j8` succeeds
- `cmake --build build_fulltests -j8 && ctest --output-on-failure` from `build_fulltests` — all pass
- No code path rewrites port names

**Commit:** `refactor: remove legacy port name fallback rewriting in Simulator`

---

### Phase F: Audit and Harden Remaining Parse Paths

**Goal:** Verify that ALL remaining parse/load/build paths are strict-only. No silent defaults for required fields.

**Entry Criteria:**
- Phase E commit is green

**Actions:**
1. Audit `json_parser.cpp` for any remaining tolerance:
   - Line 951-953: version check already strict (throws on non-3.0) ✓
   - Line 814-824: domains already mandatory (throws on missing) ✓
   - Line 989-1005: same for library loading ✓
   - Line 1201-1206: merge_device_instance already strict ✓
2. Audit `blueprint_codec.cpp`:
   - Line 1036-1042: version check already returns error ✓
   - Line 1069-1078: unknown top-level fields rejected ✓
3. Audit `codegen.cpp`:
   - Line 106: already strict ✓
4. Audit TimeDelay `"delay"` convenience param (jit_solver.cpp:721-739):
   - **Decision:** This is a legitimate convenience API (sets both `delay_on` and `delay_off`). **Keep** — it's not a legacy shim but an explicit param alias. However, ensure the library `TimeDelay.blueprint` uses `delay_on`/`delay_off` in `param_defaults`, not `delay`.
5. Add negative tests for strict rejection:
   - `PushBuildValidation.MissingDomainsThrows`
   - `PushBuildValidation.UnknownParamThrows`
   - `PushBuildValidation.MissingRequiredParamThrows`

**Exit Criteria:**
- `cmake --build build -j8` succeeds
- All new negative tests pass
- Manual audit confirms no silent fallback in parse/load/build

**Commit:** `test: add strict-rejection negative tests for build validation`

---

### Phase G: Documentation Cleanup and Issue Closure

**Goal:** Update knowledge base to reflect zero-legacy state.

**Entry Criteria:**
- Phase F commit is green

**Actions:**
1. Update `knowledge/errors_TODO.md`:
   - Close Issue #14 (Zero-Fallback Metadata Cutover) — mark as COMPLETED
   - Update Issue #16 (Runtime API Simplification) status if affected
2. Update `knowledge/16_push_migration_plan.md`:
   - Add final status update: all legacy paths removed
   - Mark remaining work as COMPLETED
3. Update `knowledge/10_quick_reference.md`:
   - Remove "Legacy — for metadata only" qualifier from Domain Values table
   - Remove `solve_electrical`, `solve_logical` etc from component template example
   - Ensure runtime API section shows only `execute()` + `commit()` + `pre_load()`
4. Add this plan file to `knowledge/index.md` under Practical Notes

**Exit Criteria:**
- All knowledge files consistent with zero-legacy state
- No file in `knowledge/` references legacy solver paths as active

**Commit:** `docs: close zero-legacy cutover, update knowledge base`

---

## 5. Safety Rails

### Grep Guards (Run After Every Phase)

```bash
# Must return ZERO hits after full cutover:
grep -rn "known_library_unused\|known_unused_params" src/
grep -rn "is_migrated_component_class\|is_migrated" src/jit_solver/jit_solver.cpp
grep -rn "observe_electrical" src/jit_solver/components/
grep -rn "backward.compat" src/jit_solver/
grep -rn "\.ext\"" src/jit_solver/simulator.cpp

# Must return ZERO hits in library (after Phase A):
grep -rn "inv_internal_r\|inv_capacity\|port_edge\|exposed_direction\|exposed_type" library/**/*.blueprint
# Note: "resistance" grep needs care — check only param_defaults, not interface port names

# Allowed hits (not legacy):
# - "fallback" in router.h (L-shape routing), node.h (UI layout), parse_number.h (strtod)
# - "compatible" in port validation (active validation, not legacy shim)
# - "fallback" in codegen.cpp (MSVC compiler switch, not data model)
# - "fallback" in editor commands (layout positioning)
```

### CI Enforcement (Post-Cutover)

Add to CI pipeline or pre-commit hook:

```bash
#!/bin/bash
# zero-legacy-check.sh — fail CI if legacy patterns reappear
set -e

FORBIDDEN_PATTERNS=(
    "known_library_unused"
    "is_migrated_component_class"
    "observe_electrical"
    "backward.compat"
    "solve_electrical\|solve_mechanical\|solve_hydraulic\|solve_thermal\|solve_logical"
)

for pattern in "${FORBIDDEN_PATTERNS[@]}"; do
    if grep -rn "$pattern" src/jit_solver/ src/json_parser/ src/codegen/ 2>/dev/null; then
        echo "FAIL: Legacy pattern found: $pattern"
        exit 1
    fi
done

echo "PASS: No legacy patterns found"
```

---

## 6. Test Strategy

### Test Matrix

| Test Suite | Build Dir | Command | What It Validates |
|-----------|-----------|---------|-------------------|
| Push core | `build/tests` | `ctest --output-on-failure -R "PushRuntime\.\|PushBuildValidation\."` | Push scheduler, state model, build validation |
| Full suite (migration-only) | `build/tests` | `ctest --output-on-failure` | All enabled tests in push-migration config |
| Full suite (all tests) | `build_fulltests` | `cmake --build build_fulltests -j8 && ctest --output-on-failure` | All 1445+ tests including editor, bp2, codegen |

### New Tests to Add

| Test Name | Suite | Purpose |
|-----------|-------|---------|
| `PushBuildValidation.UnknownClassnameThrows` | `push_build_validation_tests` | Verify unknown classname causes hard throw |
| `PushBuildValidation.UnknownParamThrows` | `push_build_validation_tests` | Verify unconsumed param causes hard throw (no whitelist) |
| `PushBuildValidation.MissingDomainsThrows` | `push_build_validation_tests` | Verify missing domains causes hard throw |
| `PushBuildValidation.MissingRequiredParamThrows` | `push_build_validation_tests` | Verify missing required param causes hard throw |
| `PushBuildValidation.WhitelistParamsRejected` | `push_build_validation_tests` | Verify former whitelist params (`inv_internal_r`, etc.) are rejected |

### Test Commands (Exact)

```bash
# After every phase — quick validation:
cmake --build build -j8
cd build/tests && ctest --output-on-failure -R "PushRuntime\.\|PushBuildValidation\."

# After Phase A and Phase E — full validation:
cmake --build build_fulltests -j8
cd build_fulltests && ctest --output-on-failure

# Single test executable for debugging:
./build/tests/push_build_validation_tests --gtest_filter="PushBuildValidation.*"
./build/tests/push_runtime_regression_tests --gtest_filter="PushRuntime.*"
```

---

## 7. Commit Slicing Strategy

Each phase is one commit. Commits are ordered for safe incremental progress:

| # | Commit | Files Changed | Risk |
|---|--------|---------------|------|
| 1 | `chore: remove computed/unused param_defaults from library blueprints` | ~6 `.blueprint` files | Very Low |
| 2 | `refactor: remove known_library_unused_params compatibility whitelist` | `jit_solver.cpp` | Low |
| 3 | `refactor: remove is_migrated_component_class gate, add unknown-class hard fail` | `jit_solver.cpp` | Medium |
| 4 | `refactor: remove VoltageSense::observe_electrical shim` | `voltage_sense.h`, `voltage_sense.cpp` | Low |
| 5 | `refactor: remove legacy port name fallback rewriting in Simulator` | `simulator.cpp` | Low-Medium |
| 6 | `test: add strict-rejection negative tests for build validation` | `test_push_build_validation.cpp` | Low |
| 7 | `docs: close zero-legacy cutover, update knowledge base` | `knowledge/` | Zero |

**Rule:** Each commit must independently build and pass all tests. If a commit fails, fix forward — never add a temporary compatibility shim.

---

## 8. Roll-Forward Strategy

There is no rollback design. If a phase breaks something:

1. **Diagnose** which test fails and why
2. **Fix forward** — the fix must maintain zero-legacy invariants
3. If the issue is that a test itself relied on legacy behavior:
   - Update the test to use the canonical path
   - Or delete the test if it tested removed legacy functionality
4. If a library `.blueprint` has a param that a component actually reads:
   - It was wrongly placed in the whitelist — keep it in the blueprint and add a `consume_*` call
5. Never re-introduce a removed compatibility path

**Critical principle:** The whitelist existed because some params were in `.blueprint` JSON but not consumed by C++ code. The fix is to remove them from JSON (Phase A), not to keep the whitelist.

---

## 9. Definition of Done

All of the following must be true:

- [ ] `cmake --build build -j8` succeeds
- [ ] `ctest --output-on-failure -R "PushRuntime\.\|PushBuildValidation\."` from `build/tests` — 100% pass
- [ ] `cmake --build build_fulltests -j8 && ctest --output-on-failure` from `build_fulltests` — 100% pass
- [ ] All grep guards (Section 5) return zero hits
- [ ] `knowledge/errors_TODO.md` Issue #14 is marked COMPLETED
- [ ] `PushBuildValidation.UnknownClassnameThrows` test exists and passes
- [ ] `PushBuildValidation.WhitelistParamsRejected` test exists and passes
- [ ] No `.blueprint` file contains `inv_internal_r`, `inv_capacity`, `port_edge`, `exposed_direction`, or `exposed_type`
- [ ] No production code path silently ignores an unknown parameter
- [ ] No production code path rewrites port names for legacy compatibility
- [ ] Zero `observe_electrical` declarations/definitions exist in component code

---

## 10. Mechanical Work Queue

Optimized for sequential execution by a boring_work agent. Each task is atomic, testable, and independent enough to commit separately or batch with adjacent tasks.

### Phase A: Library Blueprint Normalization

- [ ] A-1: Read `library/electrical/Battery.blueprint`, remove `inv_internal_r` and `inv_capacity` from `param_defaults`
- [ ] A-2: Read `library/electrical/Generator.blueprint`, check for and remove `inv_internal_r` from `param_defaults`
- [ ] A-3: Read `library/Bus.blueprint`, check for and remove `port_edge` from `param_defaults`
- [ ] A-4: Read `library/BlueprintInput.blueprint`, check for and remove `exposed_direction` and `exposed_type` from `param_defaults`
- [ ] A-5: Read `library/electrical/Load.blueprint`, check for and remove `resistance` from `param_defaults`
- [ ] A-6: Run `grep -rn "inv_internal_r\|inv_capacity\|port_edge\|exposed_direction\|exposed_type" library/` to verify all removed
- [ ] A-7: Run `cmake --build build -j8` — verify build succeeds
- [ ] A-8: Run `ctest --output-on-failure -R "PushRuntime\.\|PushBuildValidation\."` from `build/tests` — verify pass

### Phase B: Remove Compatibility Whitelist

- [ ] B-1: Delete `known_library_unused_params()` function body (jit_solver.cpp ~lines 131-141)
- [ ] B-2: Delete `const auto& known_unused_params = known_library_unused_params();` line (~289)
- [ ] B-3: In `validate_all_params_consumed` lambda, remove the `known_unused_params.find(key)` branch (~lines 336-340)
- [ ] B-4: Remove the `// to maintain backward compatibility` comment (~line 288)
- [ ] B-5: Run `grep -rn "known_library_unused\|known_unused_params\|backward.compat" src/` — verify zero hits
- [ ] B-6: Run `cmake --build build -j8 && ctest --output-on-failure` from `build/tests`

### Phase C: Remove Migration Gate

- [ ] C-1: Delete `is_migrated_component_class()` function (jit_solver.cpp ~lines 92-129)
- [ ] C-2: Delete `bool is_migrated = is_migrated_component_class(dev.classname);` (~line 273)
- [ ] C-3: Delete `if (!is_migrated) { continue; }` guard (~lines 275-277)
- [ ] C-4: Add final `else { throw std::runtime_error("Unknown component class..."); }` at end of if/else chain
- [ ] C-5: Run `grep -rn "is_migrated" src/jit_solver/jit_solver.cpp` — verify zero hits
- [ ] C-6: Run `cmake --build build -j8 && ctest --output-on-failure` from `build/tests`
- [ ] C-7: Add test `PushBuildValidation.UnknownClassnameThrows` to `tests/test_push_build_validation.cpp`
- [ ] C-8: Build and verify new test passes

### Phase D: Remove Shim Methods

- [ ] D-1: Remove `void observe_electrical(SimulationState& st, float dt);` from `voltage_sense.h` (line 22)
- [ ] D-2: Remove `observe_electrical()` implementation from `voltage_sense.cpp` (lines 5-10)
- [ ] D-3: Run `grep -rn "observe_electrical" src/ tests/` — verify zero hits (or update any remaining call sites)
- [ ] D-4: Run `cmake --build build -j8 && ctest --output-on-failure` from `build/tests`

### Phase E: Remove Port Name Fallback

- [ ] E-1: In `simulator.cpp`, remove the `:*.ext` fallback block in `get_wire_voltage()` (lines ~122-132)
- [ ] E-2: Simplify to: lookup → not found → return 0.0f
- [ ] E-3: Run `grep -rn "\.ext\"\|fallback.*port" src/jit_solver/simulator.cpp` — verify zero hits
- [ ] E-4: Run `cmake --build build -j8 && ctest --output-on-failure` from `build/tests`
- [ ] E-5: Run `cmake --build build_fulltests -j8 && ctest --output-on-failure` from `build_fulltests`

### Phase F: Add Negative Tests

- [ ] F-1: Add `PushBuildValidation.WhitelistParamsRejected` — pass a device with `inv_internal_r` in params, expect throw
- [ ] F-2: Add `PushBuildValidation.UnknownParamThrows` — pass a device with bogus param, expect throw
- [ ] F-3: Add `PushBuildValidation.MissingDomainsThrows` — construct a TypeDefinition with empty domains, expect throw from merge
- [ ] F-4: Add `PushBuildValidation.MissingRequiredParamThrows` — pass a component missing a required param, expect throw
- [ ] F-5: Run `cmake --build build -j8 && ctest --output-on-failure -R "PushBuildValidation\."` from `build/tests`

### Phase G: Documentation

- [ ] G-1: In `knowledge/errors_TODO.md`, mark Issue #14 as COMPLETED with date and commit ref
- [ ] G-2: In `knowledge/16_push_migration_plan.md`, add final "Remaining work: NONE" update
- [ ] G-3: In `knowledge/10_quick_reference.md`, remove "Legacy — for metadata only" from Domain Values table header
- [ ] G-4: In `knowledge/10_quick_reference.md`, update Component Template Pattern to show `execute()`/`commit()` API only
- [ ] G-5: In `knowledge/index.md`, add entry for `knowledge/blueprint_migration/zero_legacy_cutover_plan.md`
- [ ] G-6: Run all grep guards from Section 5 — verify all clean

**Total: 38 tasks**

---

## 11. Risk Register

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|-----------|--------|------------|
| R-1 | Removing whitelist params from `.blueprint` files breaks tests that construct `DeviceInstance` with those params inline | Medium | Medium | Run full test suite after Phase A; fix any test that hard-codes removed params |
| R-2 | Removing `is_migrated_component_class()` gate causes unhandled classname to silently skip (no throw) | Low | High | Phase C adds explicit final `else throw`; test verifies |
| R-3 | `:*.ext` port name fallback is used by editor simulation integration tests | Medium | Low | grep for `.ext` in test files; update callers to use canonical port names |
| R-4 | A `.blueprint` file has a param that IS consumed by the component but was wrongly added to the whitelist | Low | Medium | If `cmake --build` fails after Phase A, the param is actually consumed — keep it in the `.blueprint` and remove it from the whitelist instead |
| R-5 | AOT codegen has latent references to removed shims | Low | Medium | Phase F audit covers codegen; AOT already strict |

---

## Appendix: File Quick Reference

```
src/jit_solver/jit_solver.cpp      # Factory: component creation, param consumption, topo sort
src/jit_solver/simulator.cpp       # Runtime: step, get_wire_voltage, port lookups
src/jit_solver/scheduler.h         # PushScheduler: execute+commit loop (already clean)
src/jit_solver/state.h             # SimulationState (already clean)
src/jit_solver/components/         # All component headers/sources
src/json_parser/json_parser.cpp    # Parser: library loading, type registry, blueprint parsing
src/blueprint_v2/codec/            # bp2 codec: encode/decode blueprints
src/codegen/codegen.cpp            # AOT code generation
library/**/*.blueprint             # Component metadata (version 3.0 format)
tests/test_push_build_validation.cpp  # Build validation tests
tests/test_push_runtime_regression.cpp # Runtime regression tests
```
