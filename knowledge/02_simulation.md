# Simulation Engine

This document reflects the current runtime after electrical subsolver migration.

## Runtime Model (Current)

The simulator is now **hybrid**:

- push scheduler for logical/control/general component execution
- local electrical island subsolver for closed electrical networks

This replaced the previous pass-through-only electrical behavior that caused runaway loops.

---

## SimulationState

`SimulationState` currently stores a single signal array:

```cpp
struct SimulationState {
    std::vector<float> values;
    std::vector<SignalType> signal_types;
    std::vector<float> lut_keys;
    std::vector<float> lut_values;
    uint32_t dynamic_signals_count;

    // Valid only during Simulator::step(), null outside.
    ElectricalRuntimeState* electrical_rt;
};
```

Notes:

- `values[]` contains node values used by both push and solver-owned flows.
- `electrical_rt` is set before solve/scheduler run and cleared after step.
- Components must treat `electrical_rt == nullptr` as valid (no access outside active frame).

---

## Step Pipeline

Current `Simulator::step(dt)` sequence:

1. **Clamp dt** (`dt = std::min(dt, MAX_DT)` where `MAX_DT = 0.1`) to prevent physics explosions
2. Set `state.electrical_rt = &electrical_rt_` (RAII guard ensures cleanup)
3. **Pre-solve**: `update_dynamic_sources()` — stamp actuator states from previous frame
4. **Solve electrical**: `solve_electrical(electrical_plan, state, electrical_rt_, dt)`
5. **Push scheduler**: `scheduler.step(state, dt)` — execute all logical/mechanical/etc. components
6. **Commit pass**: `commit_solver_owned_devices()` — battery discharge, state transitions
7. Clear `state.electrical_rt` via RAII guard
8. Advance `time_ += dt`, `step_count_++`

Key consequence:

- electrical node voltages are solved before push consumers read them
- solver-owned electrical propagators do not write pass-through voltages in push phase
- one-frame delay for actuator state changes (AZS toggle, relay close) is intentional

---

## Electrical Build Plan

Build stage (`build_systems_dev`) constructs:

- `port_to_signal` map (union-find connected signals)
- `electrical_plan` with connected electrical islands

Each island stores:

- `signal_indices` (nodes)
- `elements` of kinds:
  - `FixedVoltageNode`
  - `TheveninSource`
  - `ConductanceBranch`

Extraction path is dual-mode:

1. metadata-driven (`solver_role`) when present
2. classname fallback for wrappers and direct test-created `DeviceInstance`

---

## Electrical Solver Behavior

The electrical solver runs per island:

- builds dense nodal system for unknown node voltages
- stamps conductance branches
- converts Thevenin source to Norton equivalent for stamping
- applies fixed-node constraints
- solves with dense Gaussian elimination + pivoting
- writes solved node voltages back to `SimulationState::values`
- writes branch currents to `ElectricalRuntimeState::branch_currents`

### Singular island handling

In editor/runtime, singular islands are expected during interactive edits.

Current behavior:

- does **not throw/abort** on singular solve
- preserves previous node values for that island
- zeros branch currents for that island

This prevents editor crashes while keeping valid islands solved normally.

---

## Ownership Rules (Electrical)

Solver-owned electrical propagators are not push-scheduled for electrical write behavior:

- `Battery`
- `Generator`
- `Resistor`
- `ElectricalSource`
- `ElectricalConductance`

`RefNode` remains scheduled as source for broader graph behavior, but electrical node clamping is solver-owned.

Observer-like components continue push execution for derived outputs:

- `IndicatorLight` computes `brightness` from solved voltage
- `CurrentSense` outputs solved branch current from handle

---

## Battery/Current Semantics

### Battery

- electrical propagation is solver-owned (execute no-op)
- `commit(st, dt)` uses solved branch current and updates `charge`
- sign convention: discharge uses negative Thevenin branch current (`max(0, -i)`)
- `charge` and `capacity` use `double` for stable accumulation
- live telemetry outputs are written each commit when mapped:
  - `charge_out`
  - `soc_out`

### CurrentSense

- no fake `dv * g` formula
- reads solver branch current via electrical handle
- outputs `0` if handle/runtime state is invalid

---

## Testing Guidance

Use these suites for solver/runtime regressions:

- `electrical_subsolver_tests`
- `electrical_island_build_tests`
- `electrical_handle_build_tests`
- `electrical_primitives_tests`
- `current_sense_tests`
- `battery_discharge_tests`
- `push_runtime_regression_tests`

Important runtime regressions:

- `ClosedCircuitBlueprint_NoRunawayVoltage`
- `ClosedCircuitBlueprint_BatteryChargeDecreases_RealFixture`
- `BatteryLiveOutputsExposeChargeAndSoc`
