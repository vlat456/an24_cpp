# Scheduler Refactor Plan

## Goal

Remove the artificial control-loop latency caused by the current runtime order and make JIT and AOT execute the same explicit phase plan.

Primary target:

- no extra 2-frame delay for `electrical -> control -> actuator -> electrical`
- no stale measurement signals from electrical sensors feeding logical control
- no hidden dependence on the vague catch-all `finalize_step()` phase
- identical execution semantics between JIT runtime and AOT generated code

## Why This Refactor Is Needed

Current JIT order in `src/jit_solver/simulator.cpp` is:

1. stamp electrical/mechanical/hydraulic/thermal
2. run SOR
3. run `finalize_step()`
4. run logical

Current AOT codegen in `src/codegen/codegen.cpp` emits the same order:

1. electrical and sub-rate domains
2. SOR
3. `finalize_step()`
4. logical

That ordering creates pipeline latency in closed control loops:

- electrical bus settles in SOR
- electrical observers are read later
- controllers often update in `finalize_step()`
- actuator bridges use the new command only on the next electrical pass

For `bus -> PI/PID -> LUT/filter -> ControlledVoltageSource -> bus`, that becomes a two-frame delay.

There is also a second architectural problem: some devices both participate in the electrical network and emit derived measurement/control values, but they do that in the wrong phase. Example: `CurrentSense` stamps a two-port and also writes `i_out` during electrical stamping, before SOR has settled the actual node voltages.

## Root Cause

The codebase currently overloads one concept (`Domain`) with two different meanings:

- physical simulation domain (`Electrical`, `Logical`, `Mechanical`, ...)
- runtime execution phase ordering

That works for simple devices, but breaks mixed-role components:

- `VoltageSense` is physically an electrical observer but semantically a post-SOR measurement bridge
- `ControlledVoltageSource` is physically electrical but semantically an actuator that must run after control logic
- `PI/PID/PD/P` are controllers, not electrical network elements, but they are currently tagged as `Domain::Electrical`
- `CurrentSense` is both a passive electrical element and a post-SOR observer

## Design Principles

1. Keep physical domains and execution phases separate.
2. Make phase order explicit and shared by JIT and AOT.
3. Allow one component to participate in more than one phase.
4. Keep electrical SOR semantics intact: stamp first, solve second.
5. Remove control-state updates from generic `finalize_step()` where phase-specific hooks are clearer.
6. Prefer a single shared execution-plan builder over duplicated JIT/AOT scheduling logic.
7. Make the runtime `dt`-driven; do not encode gameplay behavior around an assumed 60 Hz frame cadence.

## Timing Model

The current project often talks about 60 Hz because tests commonly use `dt = 1/60`, but that must be treated as a test convenience, not a runtime contract.

For end users, frame time may be anything:

- 120 Hz render loop
- 75 Hz VR
- unstable frame pacing
- long editor frames
- replay / fast-forward / slow-motion scenarios

The refactor must therefore preserve these rules:

1. all control, filtering, and integration code must use the provided `dt`
2. sub-rate domains must use accumulated real time, not frame counts disguised as timing
3. no phase may assume that "next frame" means `1/60` second
4. tests may still use fixed `1/60`, but production semantics must be stable for arbitrary `dt`

More precisely, this must be interpreted as accumulated simulation `dt`, not wall-clock time.

## Pause Contract

The scheduler must be pause-safe.

That means:

- all time evolution is driven only by caller-provided simulation `dt`
- accumulators advance only from simulation `dt`
- when the sim is paused, effective `dt = 0` and all accumulators stop advancing
- resuming after a long wall-clock pause must not inject one giant catch-up step unless the caller explicitly requests time skip / fast-forward behavior

This is important because the end user may pause the sim or editor at any moment.

Practical rule:

- `accumulated dt` means accumulated simulation time consumed by the solver
- it must never mean elapsed real time between UI frames

## Step Semantics

Use the word `step` to mean one invocation of the simulator with a caller-provided `dt`.

Within one step, the scheduler may execute several internal phases and more than one electrical solve, but that is still one simulation step from the caller's point of view.

If `dt <= 0`, the step must behave as a pause/no-advance step:

- no integration progress
- no accumulator growth
- no catch-up scheduling
- optionally allow pure read-only/UI refresh work outside the simulation core

Recommended terminology:

- `step(dt)` - one outer simulation update with arbitrary real-time delta
- `passive electrical pass` - first electrical stamp/solve path inside the step
- `actuator electrical pass` - second electrical stamp/solve path inside the step
- `sub-rate tick` - a moment when a slower domain consumes its accumulated time and advances
- `latched output` - the most recently published value from a slower domain, held until its next tick

## Target Runtime Model

### Phase Order Per Frame

Recommended frame pipeline:

1. `begin_step`
2. `stamp_electrical_passive`
3. `sor_passive`
4. `observe_electrical`
5. `solve_logical`
6. `commit_control`
7. `stamp_electrical_actuators`
8. `sor_actuators`
9. `tick_subrate_domains`
10. `finalize_step`

Meaning:

- `stamp_electrical_passive`: resistors, loads, switches, relays, pass-through elements, fixed sources already belonging to plant topology
- `sor_passive`: solve plant without actuator commands from this frame being deferred
- `observe_electrical`: compute measurements from the settled electrical state
- `solve_logical`: run controllers and signal-processing graph using fresh observations
- `commit_control`: apply edge-triggered logical state changes once per frame if needed
- `stamp_electrical_actuators`: stamp controlled voltage/current/conductance devices using the command just computed
- `sor_actuators`: second electrical solve in the same frame so control takes effect immediately
- `tick_subrate_domains`: advance mechanical/hydraulic/thermal only when their accumulated time reaches threshold, using the final electrical/logical state of this step
- `finalize_step`: state machines and display/bookkeeping updates that truly belong after all solves

### Expected Effect

For a regulator loop:

- bus voltage is observed after the first SOR of the same frame
- controller computes command in the same frame
- actuator stamps from that command in the same frame
- second SOR produces final bus voltage in the same frame

This removes the artificial two-frame pipeline delay while preserving explicit stamp-then-solve behavior.

## Multi-Rate Domain Contract

The new scheduler must explicitly define how fast electrical/control phases interact with slower domains such as mechanical, hydraulic, and thermal.

### Core Rule

Slow domains must be treated as `sample-and-hold` systems:

- they advance only when enough accumulated real time has elapsed
- between ticks, their outputs remain latched
- fast domains read those latched outputs every step
- when a slow domain does tick, it reads the final settled state of the current step, not an intermediate pre-control state

This avoids ambiguity about whether a 20 Hz or 1 Hz domain is seeing a pre-actuator or post-actuator electrical network.

### Recommended ordering for slow domains

Within one outer `step(dt)`:

1. electrical passive pass uses the last latched slow-domain outputs
2. first SOR settles the passive plant
3. observers and logical control run
4. actuator pass applies the new command in the same outer step
5. second SOR settles the final electrical state for this step
6. only now may slow domains tick, if their accumulators reached threshold
7. their newly computed outputs are latched for subsequent steps

This means:

- electrical/control loop is same-step responsive
- slow domains are deterministic and do not read half-updated plant state
- thermal and mechanical integration remain physically meaningful even under variable frame rate

### Why slow domains should tick after final electrical solve

Example consequences:

- `thermal` should integrate from final current/power, not from a pre-actuator provisional state
- `mechanical` should react to the final torque/drive conditions of the step
- `hydraulic` should read finalized electrical power or control state if pumps/valves depend on it

Running slow domains before the actuator pass would reintroduce an implicit one-step lag between fast and slow subsystems.

### Accumulator rules

Each slow domain keeps an accumulated time bucket:

```cpp
accumulator_mechanical += dt;
accumulator_hydraulic += dt;
accumulator_thermal += dt;
```

When the relevant threshold is met, the domain ticks using the accumulated time actually elapsed, then subtracts or consumes that amount.

Important constraints:

- do not encode slow-domain timing as `step_count % N` alone
- do not assume `dt` is constant
- do not derive accumulator growth from wall-clock frame gaps
- handle large `dt` by allowing repeated ticks or a bounded catch-up policy

Recommended thresholds:

- mechanical period = `1 / 20` s
- hydraulic period = `1 / 5` s
- thermal period = `1 / 1` s

### Catch-up policy for large `dt`

If `dt` is large enough to cross several slow-domain periods, the scheduler needs an explicit policy.

Preferred policy:

- use a `while (accumulator >= period)` loop
- execute bounded repeated ticks with `tick_dt = period`
- subtract `period` each iteration
- optionally cap max catch-up iterations per outer step for runaway frame stalls, while logging the clamp

Pause interaction:

- while paused, no catch-up debt should accumulate from wall-clock time
- only explicitly supplied simulation `dt` may create catch-up work

This is safer than a single oversized slow-domain update because many slow models are less stable with very large integration steps.

### Latched I/O rule

Slow-domain outputs must be explicitly latched.

Interpretation:

- a slow-domain tick publishes outputs once
- those outputs stay constant until the next tick
- electrical/logical phases do not observe in-progress writes from a partially updated slow solve

This should be true in both JIT and AOT.

### Components needing special review

Mixed fast/slow components must be audited carefully because they often hide coupling assumptions:

- `RU19A`
- `GS24`
- `RUG82`
- `ElectricPump`
- `GidroAccumulator`
- `FuelTank`
- `ElectricHeater`

These components may need to be split across multiple explicit hooks rather than moved wholesale into a single phase.

## Legacy Multi-Model Validation Strategy

`GS24` and `RUG82` should be treated as legacy validation models during the refactor, not as target architecture.

They are still useful because they combine:

- controller behavior
- electrical coupling
- internal state
- legacy timing assumptions

That makes them good migration sandboxes for proving the new scheduler on difficult real components before they are removed.

### Intended role of `GS24` / `RUG82`

Use them as:

- compatibility fixtures
- multi-model comparison baselines
- stress tests for explicit phase hooks
- temporary validation targets for JIT/AOT parity

Do not use them as:

- the reason to keep legacy scheduler behavior
- a source of special-case phase ordering rules
- the long-term canonical implementation of generator/regulator logic

### Three-model comparison pattern

During migration, it is useful to compare three representations of the same conceptual system:

1. legacy monolithic component (`GS24` / `RUG82` as they exist today)
2. refactored explicit-hook legacy component
3. bridge-based blueprint implementation

This gives a controlled path for validating the new runtime:

- old behavior is still measurable
- new scheduler behavior can be characterized
- final blueprint replacement can be compared against both

### Rules for using legacy models safely

1. no scheduler exceptions just for `GS24` or `RUG82`
2. no hardcoded phase-order hacks to preserve monolith quirks
3. if they need adaptation, adapt the component to the new hooks
4. if behavior differs, capture it in tests and decide intentionally whether the new behavior is a fix or a regression

### Recommended validation workflow

For both `GS24` and `RUG82`:

1. capture baseline behavior under fixed and variable `dt`
2. run the same scenario through JIT legacy path
3. run the same scenario through refactored explicit-hook path
4. run the same scenario through AOT generated path
5. eventually run the same scenario through blueprint bridge version

Compare:

- latency in steps and seconds
- steady-state voltage/current
- pause/resume behavior
- large-`dt` recovery
- slow-domain coupling behavior

### Exit strategy

The final scheduler must not depend on `GS24` or `RUG82` remaining in the tree.

Success means:

- they helped validate the migration
- they no longer require special runtime rules
- they can be deleted without changing scheduler semantics

This should be the expected end state for the refactor.

## New Execution Metadata

Add explicit execution-phase metadata instead of inferring order from `Domain` alone.

Implementation note (current state): execution phase participation is now declared in
component blueprints via an `execution` object and consumed by both JIT and AOT.

Suggested shape:

```cpp
enum class ExecutionPhase : uint32_t {
    ElectricalPassive,
    ElectricalObserver,
    Logical,
    ControlCommit,
    ElectricalActuator,
    Finalize,
    Mechanical,
    Hydraulic,
    Thermal,
};
```

And a shared descriptor/trait per component:

```cpp
struct ExecutionTraits {
    bool electrical_passive = false;
    bool electrical_observer = false;
    bool logical = false;
    bool control_commit = false;
    bool electrical_actuator = false;
    bool finalize = false;
    bool mechanical = false;
    bool hydraulic = false;
    bool thermal = false;
};
```

Important: keep existing `Domain` flags for port typing and sub-rate physics identity, but stop using them as the only scheduler input.

## New Component Hook Model

Replace the current implicit hook contract with explicit optional hooks:

- `stamp_electrical_passive(SimulationState&, float)`
- `observe_electrical(SimulationState&, float)`
- `solve_logical(SimulationState&, float)`
- `commit_control(SimulationState&, float)`
- `stamp_electrical_actuator(SimulationState&, float)`
- `finalize_step(SimulationState&, float)`
- `solve_mechanical(SimulationState&, float)`
- `solve_hydraulic(SimulationState&, float)`
- `solve_thermal(SimulationState&, float)`

Notes:

- not every component needs every hook
- one component may implement more than one hook
- `finalize_step()` should become a temporary compatibility shim, then be deleted

## Component Migration Map

### Pure passive electrical plant

Stay in `stamp_electrical_passive`:

- `Battery`
- `Generator`
- `Load`
- `Resistor`
- `HighPowerLoad`
- `Switch`
- `Relay`
- `AZS` electrical stamping part
- `Bus`, `BlueprintInput`, `BlueprintOutput` as no-op aliases

### Observer / measurement components

Move derived outputs to `observe_electrical`:

- `VoltageSense` - observer only
- `CurrentSense` - keep series stamp in passive phase, move `i_out` write to observer phase
- `Voltmeter` - visual-only observer
- any other electrical sensor producing scalar outputs

### Logical control components

Move to `solve_logical` and remove fake electrical classification:

- `P`
- `PI`
- `PD`
- `PID`
- `FastTMO`
- `AsymTMO`
- `SlewRate`
- `Clamp`
- `LUT`
- `Multiply`
- `Add`
- `Subtract`
- `Divide`
- comparators and logic gates

For controllers with internal integrator/derivative state, update that state inside `solve_logical`, not in a catch-all post phase.

### Control commit / edge-triggered logical state

Use `commit_control` only for components that need one once-per-frame edge or state transition after logical outputs are known:

- `HoldButton`
- toggle/edge-driven logical latches if any
- legacy components that must observe finalized logical outputs before changing mode

### Electrical actuators

Move to `stamp_electrical_actuator`:

- `ControlledVoltageSource`
- `ControlledCurrentSource`
- `VariableConductance`
- any future logical-to-electrical bridge actuator

### Finalize-only state machines

Keep only truly final state updates in `finalize_step`:

- `DMR400`
- `RU19A`
- `GS24`
- `RUG82` during migration only
- thermal/hydraulic devices with per-frame memory that must see final solved values

Long-term goal: legacy monolithic controller components (`GS24`, `RUG82`) should be retired in favor of bridge-based blueprints or rewritten against the new hooks.

## JIT Refactor Plan

### 1. Build explicit phase buckets

`BuildResult` now uses phase buckets:

```cpp
struct PhaseComponents {
    std::vector<ComponentVariant*> electrical_passive;
    std::vector<ComponentVariant*> electrical_observer;
    std::vector<ComponentVariant*> logical;
    std::vector<ComponentVariant*> control_commit;
    std::vector<ComponentVariant*> electrical_actuator;
    std::vector<ComponentVariant*> finalize;
    std::vector<ComponentVariant*> mechanical;
    std::vector<ComponentVariant*> hydraulic;
    std::vector<ComponentVariant*> thermal;
};
```

Populate these in `src/jit_solver/jit_solver.cpp` using shared execution traits, not by overloading `Domain`.

### 2. Rewrite `Simulator::step()` around explicit phases

`src/jit_solver/simulator.cpp` should:

- clear accumulators once at frame start
- stamp passive electrical devices
- run first SOR
- run observers
- run logical
- run control commit
- clear electrical accumulators for actuator pass only if needed by chosen implementation
- stamp electrical actuators
- run second SOR
- tick sub-rate domains against final settled state using accumulated `dt`
- run finalize

Implementation detail to decide during coding:

- either keep separate `clear_through()` plus restamp passive + actuators into two full electrical passes
- or split electrical buffers into passive and actuator contributions and combine them before each SOR

Preferred first implementation: two explicit passes with re-stamp, because it is easier to reason about and matches physics better than trying to patch deltas into old buffers.

### 3. Remove scheduler ambiguity

Delete comments and assumptions that say logical must always run after `finalize_step()`.

Update:

- `src/jit_solver/simulator.cpp`
- `src/jit_solver/systems.h`
- `src/jit_solver/systems.cpp`

### 4. Preserve sub-rate domains

Mechanical/hydraulic/thermal scheduling can remain period-based, but must become fully `dt`-driven and latch-based.

Concretely:

- they must tick from accumulated real time, not frame count assumptions
- that accumulated time must come from simulation `dt`, not wall-clock delta
- they must read the final settled state after the actuator electrical pass
- they must publish latched outputs for the next outer steps
- large `dt` behavior must be defined via repeated fixed-period ticks or an explicit capped catch-up policy

## AOT / Codegen Refactor Plan

This is not optional. If JIT changes and AOT keeps the old order, the project will become impossible to reason about.

### 1. Stop hardcoding legacy phase order in generated code

`src/codegen/codegen.cpp` currently emits:

- electrical
- sub-rate domains
- SOR
- `finalize_step`
- logical

and hardcodes a `has_finalize_step` classname set.

That must be replaced with generated explicit phase sections built from the same shared execution traits used by JIT.

### 2. Generate phase-specific device lists

For each generated composite, codegen should emit ordered device lists for:

- passive electrical devices
- observer devices
- logical devices
- control-commit devices
- actuator devices
- finalize devices
- sub-rate domain devices

### 3. Emit two electrical solve sections per frame

Generated `step_N()` methods should follow the same structure as JIT:

1. passive stamp
2. first SOR
3. observer
4. logical
5. control commit
6. actuator stamp
7. second SOR
8. slow-domain tick/catch-up
9. finalize

Generated code must also preserve the same accumulator and latch semantics as JIT for arbitrary caller `dt`.

### 4. Remove the generated `has_finalize_step` classname allowlist

It is brittle and will become wrong as soon as hooks move.

Use capability-based codegen instead:

- if component traits say it participates in `finalize`, emit `finalize_step`
- if it participates in `control_commit`, emit `commit_control`
- if it participates in both passive and observer phases, emit both

### 5. Keep a single source of truth for scheduling metadata

Do not re-encode per-class rules separately in:

- JIT build buckets
- AOT codegen grouping
- editor warnings/validation

Recommended approach:

- add shared execution metadata in a header consumed by both JIT builder and codegen
- or generate execution metadata alongside the port registry

### 6. Update generated API docs and examples

`knowledge/06_code_generation.md` and any example loops must show the new two-pass electrical frame.

## Validation / Blueprint Implications

Scheduler fixes alone are not enough if the graph still allows illegal domain coupling.

Validation changes to pair with this refactor:

- reject direct electrical-to-logical wiring except through explicit observer bridges
- reject direct logical-to-electrical actuation except through explicit actuator bridges
- keep explicit fan-out through `Splitter`
- warn on control loops using passive measurement outputs in the wrong phase during migration

This keeps blueprint semantics aligned with the new runtime architecture.

## Migration Strategy

### Phase 0 - Lock behavior with tests

Add regression tests before refactoring:

- closed-loop regulator latency test measuring frame delay
- `VoltageSense` freshness test
- `CurrentSense.i_out` freshness test
- AOT vs JIT equivalence test on a small regulator graph
- legacy electrical-only smoke tests
- variable-`dt` regulator stability test
- pause/resume test (`dt = 0` holds all accumulators and states steady)
- slow-domain latch test (`mechanical` / `thermal` outputs hold between ticks)
- slow-domain final-state test (slow domain reads post-actuator electrical state)
- large-`dt` catch-up policy test

### Phase 1 - Introduce execution traits and phase buckets

No behavior change yet. Build metadata only.

### Phase 2 - Add new hooks with compatibility shims

Temporarily support:

- old `solve_electrical`
- old `finalize_step`
- new explicit phase hooks

Map old hooks into default phases during migration.

### Phase 3 - Migrate bridge and controller components first

Move first:

- `VoltageSense`
- `ControlledVoltageSource`
- `ControlledCurrentSource`
- `VariableConductance`
- `P/PI/PD/PID`
- `CurrentSense`

This delivers the main latency fix early.

### Phase 4 - Switch JIT runtime to new phase order

Keep AOT disabled for affected tests until codegen matches the same model.

### Phase 5 - Update AOT codegen

Generated composites must become behaviorally identical to JIT.

### Phase 6 - Remove legacy scheduler paths

Delete:

- old assumptions in `Systems`
- classname-based `has_finalize_step` codegen logic
- controller `Domain::Electrical` abuse
- compatibility shims no longer needed

### Phase 7 - Rewrite or delete legacy monoliths

Candidates:

- `GS24`
- `RUG82`

These should either become explicit-phase components or be replaced by blueprint implementations using the bridge architecture.

During migration they may remain as multi-model validation fixtures, but the scheduler must not depend on their legacy assumptions.

## Risks

### 1. Double-SOR cost

Two electrical solves per frame cost more CPU.

Mitigation:

- limit second pass to actuator-aware circuits first if needed
- profile JIT and AOT on representative systems
- optimize later only after correctness is locked

### 2. Hidden dependencies on `finalize_step()` order

Some components may accidentally rely on the old sequencing.

Mitigation:

- migrate in small batches
- add per-component phase review
- keep temporary compatibility wrappers with loud TODO markers

### 3. JIT/AOT drift during migration

Mitigation:

- gate merges on JIT/AOT equivalence tests
- share traits and phase planning code

### 4. Generated code complexity

Mitigation:

- prefer codegen from precomputed phase lists rather than nesting more conditionals into emitted C++

## Acceptance Criteria

The refactor is complete when:

- regulator loop no longer shows artificial 2-frame latency
- `VoltageSense` and `CurrentSense` outputs are same-frame fresh
- `PI/PID/PD/P` are not classified as electrical just to get scheduled
- JIT and AOT use the same explicit phase model
- codegen no longer hardcodes a classname `finalize_step` allowlist
- old scheduler comments and legacy assumptions are removed
- bridge-based blueprints behave predictably and match the documented model
- behavior remains stable for variable caller `dt`, not just `1/60`
- slow domains use documented latch/catch-up semantics and observe final same-step plant state
- pause/resume does not create phantom accumulated time from wall-clock delay
- `GS24` / `RUG82` require no scheduler special-casing and can be removed in the final cleanup

## Recommended First Deliverable

The highest-value first milestone is:

1. explicit execution traits
2. bridge/controller migration
3. same-frame second electrical pass in JIT
4. matching AOT generation

That is the smallest slice that fixes the real control-loop bug instead of only moving code around.
