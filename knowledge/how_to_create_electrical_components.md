# How To Create Electrical Components

Practical guide for creating electrical components in the current architecture.

## Decide Component Type First

Choose one path:

1. **Primitive (recommended)**
   - maps directly to solver element via `solver_role` metadata
   - minimal/no wrapper logic
2. **Wrapper (transitional)**
   - C++ behavior + classname fallback extraction in builder
   - use only if primitive metadata path is insufficient today

For new work, prefer primitives.

---

## Supported Electrical Solver Roles

Current role kinds:

- `FixedVoltageNode`
- `TheveninSource`
- `ConductanceBranch`

If your component cannot map to these, it is out of scope for current MVP and needs architecture extension.

---

## Step-by-Step: Primitive Electrical Component

## 1) Add library blueprint definition

Create `library/electrical/MyElectricalThing.blueprint`.

Required fields:

- `id`
- `scheduler_source`
- `interface`
- `domains`
- `cpp_class`
- `param_defaults`
- `solver_role`

Example (conductance branch):

```json
{
  "version": "3.0",
  "id": "ElectricalConductanceLike",
  "display_name": "Electrical Conductance Like",
  "scheduler_source": false,
  "interface": [
    { "name": "v_in",  "domain": 1, "direction": 0, "type": "V", "source_writer": false },
    { "name": "v_out", "domain": 1, "direction": 1, "type": "V", "source_writer": false }
  ],
  "cpp_class": true,
  "domains": ["Electrical"],
  "param_defaults": {
    "conductance": "0.1"
  },
  "solver_role": {
    "kind": "ConductanceBranch",
    "ports": { "a": "v_in", "b": "v_out" },
    "params": { "g": "conductance" }
  }
}
```

## 2) Add C++ component files

Add header/source under `src/jit_solver/components/`.

For solver-owned primitives, `execute`/`commit` are usually no-op:

```cpp
template <typename Provider>
void ElectricalConductanceLike<Provider>::execute(SimulationState&, float) {}

template <typename Provider>
void ElectricalConductanceLike<Provider>::commit(SimulationState&, float) {}
```

If you need derived outputs (observer behavior), compute those from solved values in `execute`.

## 3) Register in component includes/variant

Update:

- `src/jit_solver/components/all.h`
- relevant variant registration paths in `jit_solver` build logic

## 4) Regenerate port registry

Run:

```bash
cmake --build build --target regenerate_port_registry
```

This updates `src/jit_solver/components/port_registry.h`.

## 5) Ensure builder path recognizes component

If `solver_role` metadata is present and loaded through parser pipeline, extraction should use metadata path.

If tests build raw `DeviceInstance` directly (without library merge), fallback classname extraction may still be needed.

## 6) Add tests

Minimum tests:

1. build extraction kind/params are correct
2. primitive-only circuit solves expected voltages/currents
3. mixed primitive + wrapper circuit remains stable
4. metadata validation failures are clear (missing role keys/ports/params)

Existing suites to extend:

- `tests/test_electrical_primitives.cpp`
- `tests/test_electrical_island_build.cpp`
- `tests/test_electrical_subsolver.cpp`

---

## Step-by-Step: Wrapper Electrical Component (When Needed)

Use this path only when primitive role mapping is not enough.

1. Implement component class with runtime state/outputs as needed.
2. Keep electrical propagation solver-owned where possible.
3. Add explicit extraction mapping in `src/jit_solver/jit_solver.cpp` (classname fallback path).
4. Add handle assignment if component needs branch current feedback.
5. Add regression tests for both extraction and runtime behavior.

Avoid adding new pass-through electrical writes in push phase.

---

## CurrentSense Pattern (Reference)

`CurrentSense` currently demonstrates correct observer behavior:

- extraction maps it to `ConductanceBranch`
- runtime reads solved branch current by handle
- no local fake current formula

Use this pattern for future electrical meters/probes.

---

## Battery Pattern (Reference — Composite Approach)

The `12SAM28` battery is now a **pure composite blueprint** (`library/systems/12SAM28.blueprint`), not a C++ class. It demonstrates how to build complex electrical subsystems from primitives:

- **ControlledVoltageSource**: Thevenin source (solver-owned, reads `cmd` from signal array)
- **CurrentSense**: measures branch current (solver-owned, writes `i_out` to signal array)
- **Multiply + Accumulator**: coulomb counting (integrates current to get charge in Ah)
- **Normalize + LUT**: SOC → OCV feedback loop
- **Splitter**: fans out signals for multiple consumers (one-to-one wiring constraint)

Key design points:

1. **One-frame delay in feedback**: CVS reads `cmd` in `update_dynamic_sources` (phase 1), but LUT writes new `cmd` in `scheduler.step` (phase 3). Tests need 2 warmup steps.
2. **Port naming**: After expansion as device `sb`, ports are `sb:v_out.ext` (not `sb.v_out`). `get_port_value("sb", "v_out")` handles this automatically.
3. **Accumulator initial value**: Set via `initial_val` param (28 Ah). First-frame cold-start logic snaps to this value.

If adding similar stateful sources, prefer composites over new C++ classes. Use `commit()` for state integration in primitive components.

---

## Common Pitfalls

1. forgetting to regenerate `port_registry.h`
2. adding new ports but not writing outputs in component runtime
3. relying on direct `build_systems_dev()` tests without realizing `solver_role` may not be populated there
4. reintroducing electrical pass-through writes for solver-owned components
5. using float accumulators for tiny long-run deltas (use `double` for accumulated state)

---

## Quick Checklist

- [ ] blueprint has valid `solver_role`
- [ ] ports/params mapped correctly in role metadata
- [ ] C++ component added and included
- [ ] port registry regenerated
- [ ] extraction path verified (metadata and/or fallback)
- [ ] regression tests added
- [ ] no push pass-through electrical writes for solver-owned propagation
