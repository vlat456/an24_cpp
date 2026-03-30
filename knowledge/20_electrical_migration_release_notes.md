# Electrical Migration Release Notes

Date: 2026-03-31

This release completes the electrical migration from push pass-through behavior to a local electrical island subsolver.

## What changed

- Added a dedicated electrical subsolver (`FixedVoltageNode`, `TheveninSource`, `ConductanceBranch`) with per-island solve and branch-current output.
- Integrated solver execution into simulator step flow before push scheduler execution.
- Migrated electrical propagation ownership away from push writes for solver-owned electrical components.
- Added robust singular-island handling so editor/runtime no longer abort on malformed temporary circuits.
- Added primitive-first electrical nodes:
  - `ElectricalSource`
  - `ElectricalConductance`
- Added minimal metadata-driven solver-role extraction (`solver_role`) with validated fallback path for direct test-created `DeviceInstance` inputs.

## Component behavior updates

- `Battery`
  - Uses solved branch current in `commit(st, dt)` for discharge integration.
  - `charge`/`capacity` are `double` for stable long-run accumulation.
  - Added live telemetry ports:
    - `charge_out`
    - `soc_out`
- `CurrentSense`
  - Now outputs solved branch current via electrical handle.
  - Removed local fake `dv * g` current approximation.
- `IndicatorLight`
  - Observer-style behavior: brightness from solved voltage.
  - No electrical pass-through write.
- `Resistor` / `Generator`
  - Electrical propagation is solver-owned (execute no-op for propagation).

## Testing and validation

Added and expanded dedicated suites:

- `electrical_subsolver_tests`
- `electrical_island_build_tests`
- `electrical_handle_build_tests`
- `electrical_primitives_tests`
- `current_sense_tests`
- `battery_discharge_tests`
- push-runtime regressions for real closed-circuit fixture behavior and battery telemetry

Key regressions covered:

- no runaway voltage in closed loop fixture
- battery discharge from solved current
- current-sense solved-current path
- primitive node extraction and solve correctness
- singular island safety (no hard abort)

## Notes for users

- Battery terminal voltage may appear visually near-constant unless load current or internal resistance is large enough; this is expected because only internal-R drop is modeled today (no SoC-to-OCV sag curve yet).
- For live battery discharge monitoring in editor, use:
  - `battery.<name>.charge_out`
  - `battery.<name>.soc_out`
