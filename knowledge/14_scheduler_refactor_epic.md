# Scheduler Refactor Epic

## Epic: Refactor scheduler to explicit phase model with JIT/AOT parity

### Context

- The current scheduler creates artificial 2-frame delay in the loop `electrical -> control -> actuator -> electrical`.
- The root cause is architectural phase ordering, not one isolated component bug.
- Runtime currently leans too hard on frame cadence assumptions, while production must behave correctly for arbitrary caller `dt`.
- All time-based processes must live only on accumulated simulation `dt`.
- Pause must mean `dt = 0`, with no wall-clock debt accumulation.
- JIT and AOT must have identical execution semantics.
- `GS24` and `RUG82` are legacy components; they may be used as migration sandboxes and multi-model validation fixtures, but final scheduler semantics must not depend on them.

### Goal

- Move core to an explicit phase scheduler.
- Remove artificial pipeline latency.
- Make runtime fully `dt`-driven and pause-safe.
- Define sample-and-hold contract for slow domains.
- Keep JIT and AOT execution models aligned.
- Prepare the project for eventual removal of `GS24` / `RUG82`.

### Non-goals

- Do not preserve legacy ordering just for compatibility without tests.
- Do not add scheduler exceptions for `GS24` / `RUG82`.
- Do not postpone JIT/AOT parity indefinitely.
- Do not bind runtime semantics to 60 Hz; `1/60` is only a test convention.

### Source Plan

- `knowledge/13_scheduler_refactor_plan.md`

## Target Semantics

- One outer `step(dt)` may contain multiple internal phases and two electrical solves.
- All integration and timing must use caller-provided simulation `dt`.
- If `dt <= 0`, simulation must not advance.
- Slow domains (`mechanical`, `hydraulic`, `thermal`) must use accumulated simulation `dt`.
- Slow domains must tick from the final settled state of the current step.
- Slow-domain outputs must be latched between ticks.
- Large `dt` must use an explicit catch-up policy, not implicit wall-clock behavior.

## Target Phase Pipeline

1. `begin_step`
2. `stamp_electrical_passive`
3. `sor_passive`
4. `observe_electrical`
5. `solve_logical`
6. `commit_control`
7. `stamp_electrical_actuator`
8. `sor_actuators`
9. `tick_subrate_domains`
10. `finalize_step`

## Stage 0 - Lock current and target behavior with tests

- [x] Add closed-loop regulator latency regression test
- [x] Add `VoltageSense` same-step freshness test
- [x] Add `CurrentSense.i_out` post-SOR freshness test
- [x] Add variable-`dt` regulator stability test
- [x] Add pause/resume test with `dt = 0`
- [x] Add slow-domain latch behavior test
- [x] Add slow-domain final-state-read test
- [x] Add large-`dt` catch-up policy test
- [x] Add JIT vs AOT equivalence test on a minimal bridge-based regulator graph

Notes:

- Tests should express behavior in seconds/step semantics, not only frame counts.
- Prefer small deterministic fixtures.

## Stage 1 - Introduce explicit execution metadata

- [x] Add shared `ExecutionPhase` model
- [x] Add shared `ExecutionTraits` model
- [x] Add a single source of truth for per-component phase capabilities
- [x] Make both JIT builder and AOT codegen consume the same metadata

Implementation notes:

- Keep existing `Domain` flags for physical/domain identity and port typing.
- Stop using `Domain` as the only scheduler truth.

## Stage 2 - Add explicit hooks with compatibility shim

- [x] Add optional hooks:
  - `stamp_electrical_passive`
  - `observe_electrical`
  - `solve_logical`
  - `commit_control`
  - `stamp_electrical_actuator`
  - `finalize_step`
  - existing slow-domain hooks stay
- [x] Add temporary compatibility mapping from legacy hooks
- [x] Keep build green without changing full scheduler yet

Implementation notes:

- Temporary mapping:
  - old `solve_electrical` -> passive phase
  - old `finalize_step` -> finalize phase
- Mark shim path clearly as temporary.

## Stage 3 - Migrate bridge/control components first

- [x] Migrate `VoltageSense`
- [x] Migrate `ControlledVoltageSource`
- [x] Migrate `ControlledCurrentSource`
- [x] Migrate `VariableConductance`
- [x] Migrate `P`
- [x] Migrate `PI`
- [x] Migrate `PD`
- [x] Migrate `PID`
- [x] Migrate `CurrentSense`
- [x] Migrate `Voltmeter`

Expected outcome:

- Controllers stop pretending to be electrical.
- Observers produce same-step fresh values.
- Actuators stamp in an explicit post-control phase.

## Stage 4 - Build explicit phase buckets in JIT

- [x] Replace/extend `domain_components` with phase buckets
- [x] Add buckets for:
  - `electrical_passive`
  - `electrical_observer`
  - `logical`
  - `control_commit`
  - `electrical_actuator`
  - `finalize`
  - `mechanical`
  - `hydraulic`
  - `thermal`
- [x] Ensure bucket assignment uses shared execution metadata only

Expected outcome:

- JIT runtime can iterate by execution phase, not by overloaded domain meaning.

## Stage 5 - Rewrite JIT scheduler

- [x] Rewrite `Simulator::step(dt)` to explicit phase order
- [x] Use two electrical passes in one outer step
- [x] Make all timing depend only on simulation `dt`
- [x] Ensure `dt <= 0` causes no simulation advance
- [x] Ensure slow domains tick only after second electrical solve
- [x] Ensure slow domains read final settled state
- [x] Ensure slow-domain outputs are latched between ticks

Implementation notes:

- Prefer first implementation as two explicit electrical passes with restamp.
- Avoid clever partial residual reuse until semantics are locked.

## Stage 6 - Slow-domain accumulated-`dt` cleanup

- [x] Remove frame-count-based behavior as scheduler truth
- [x] Implement accumulated simulation `dt` for `mechanical`
- [x] Implement accumulated simulation `dt` for `hydraulic`
- [x] Implement accumulated simulation `dt` for `thermal`
- [x] Define repeated-tick catch-up policy for large `dt`
- [x] Add optional max catch-up clamp with logging
- [x] Ensure pause does not accumulate wall-clock debt

Special review targets:

- [x] `RU19A`
- [x] `GS24`
- [x] `RUG82`
- [x] `ElectricPump`
- [x] `GidroAccumulator`
- [x] `FuelTank`
- [x] `ElectricHeater`

## Stage 7 - Bring AOT/codegen to parity immediately

- [x] Replace legacy generated phase order in `src/codegen/codegen.cpp`
- [x] Remove classname-based `has_finalize_step` allowlist
- [x] Generate phase-specific device lists from shared execution metadata
- [x] Generate two electrical solve sections per outer step
- [x] Generate slow-domain tick/catch-up semantics identical to JIT
- [x] Keep pause/`dt=0` semantics identical to JIT
- [x] Re-run JIT vs AOT equivalence tests

Critical note:

- Do not leave AOT on legacy ordering after JIT scheduler switch.

## Stage 8 - Legacy cleanup

- [x] Remove compatibility shim from hot path
- [ ] Remove old scheduler assumptions from runtime comments/docs
- [x] Remove controller `Domain::Electrical` abuse
- [x] Remove legacy domain-only scheduling logic
- [ ] Simplify/remove obsolete `Systems` APIs if no longer needed

Notes:

- Scheduling no longer depends on `Domain` classification (execution metadata is source of truth).
- Remaining cleanup is API-level simplification/deprecation of `Systems` if no longer needed.

## Stage 9 - Use `GS24` / `RUG82` as multi-model validation, then retire

- [ ] Capture baseline behavior for `GS24` and `RUG82` under fixed and variable `dt`
- [ ] Compare legacy monolith vs explicit-hook migration version
- [ ] Compare JIT vs AOT for these cases
- [ ] Compare against bridge-based blueprint version where available
- [ ] Confirm no scheduler special-casing is required
- [ ] Remove them in final cleanup or demote them to legacy-only fixtures

Important:

- `GS24` and `RUG82` are validation sandboxes, not design anchors.
- Scheduler semantics must not depend on them.

## Stage 10 - Documentation sync

- [x] Update `knowledge/02_simulation.md`
- [x] Update `knowledge/06_code_generation.md`
- [x] Update `knowledge/11_domain_bridges.md`
- [x] Update `knowledge/13_scheduler_refactor_plan.md` if implementation diverged
- [x] Document pause-safe accumulated-`dt` contract
- [x] Document latch/catch-up semantics for slow domains
- [x] Document how new components declare execution phases

## Acceptance Criteria

- [x] Closed-loop regulator no longer exhibits artificial 2-frame latency
- [x] `VoltageSense` and `CurrentSense` are same-step fresh
- [x] Controllers are no longer classified as electrical just to be scheduled
- [x] All runtime timing semantics are based on simulation `dt`, not wall-clock gaps
- [x] `dt = 0` pause does not advance state or accumulate hidden debt
- [x] Slow domains use explicit latch/catch-up semantics
- [x] Slow domains read final same-step plant state
- [x] JIT and AOT execute equivalent phase ordering
- [x] Codegen no longer hardcodes classname-based `finalize_step` scheduling
- [x] `GS24` / `RUG82` require no scheduler special-casing
- [ ] Legacy components can be deleted without changing scheduler semantics

## Suggested PR Split

1. tests only
2. execution traits + hook scaffolding
3. bridge/controller migration
4. JIT phase buckets
5. JIT scheduler rewrite
6. slow-domain accumulated-`dt` cleanup
7. AOT/codegen parity
8. legacy cleanup
9. `GS24` / `RUG82` validation and retirement
10. docs sync

## Instructions for Coding Agent

- Prefer semantics-first changes over micro-optimizations.
- Keep JIT and AOT aligned at every meaningful milestone.
- If behavior changes, add/update tests in the same stage.
- Do not preserve legacy quirks silently; document whether a difference is an intentional fix or a regression.
- Avoid introducing wall-clock timing into core simulation.
- Treat `knowledge/13_scheduler_refactor_plan.md` as architecture spec unless implementation reveals a better approach; if so, update the doc in the same PR.
