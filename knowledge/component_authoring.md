# Component Authoring Guide

This guide is for writing components that behave well in the current solver.

Relevant code:

- `src/core/solvers/jit/components/all.h`
- `src/core/solvers/common/provider.h`
- `src/core/solvers/jit/state.h`
- `src/core/solvers/jit/build_factory.cpp` (AUTO-GENERATED — do not edit)
- `src/core/solvers/aot/codegen_registry.cpp` (codegen tool that produces the factory)

## Main rule

Write components so they are numerically boring.

That means:

- stamp conductance/current cleanly
- avoid hidden feedback inside `solve_*()`
- keep defaults moderate
- prefer stable approximation over perfect physical purity

## Component template pattern

Use the existing pattern:

```cpp
template <typename Provider = JitProvider>
class MyComponent {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;

    float param = 1.0f;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
```

## Rules

### 1. `execute()` should compute outputs, `commit()` should update state

Do not update persistent internal state in `execute()`. Use `commit()` for state transitions.

Bad:

```cpp
void execute(SimulationState& st, double dt) {
    integrator_state += error * dt;
    st.values[provider.get(PortNames::out)] = integrator_state;
}
```

Good:

```cpp
void execute(SimulationState& st, double /*dt*/) {
    st.values[provider.get(PortNames::out)] = integrator_state;
}

void commit(SimulationState& st, double dt) {
    integrator_state += error * dt;
}
```

Why:

- `execute()` runs during scheduler step
- state changes in `commit()` take effect next frame (one-frame delay semantics)
- this prevents combinatorial feedback loops

### 2. Electrical components participate in subsolver, not push

Components that model electrical elements (resistors, sources, etc.) don't use `execute()` for electrical stamping. Instead:

- They contribute elements to the **electrical build plan** at build time
- The **electrical subsolver** solves node voltages for connected islands
- Components read solved values via `st.electrical_rt` in `execute()` or `commit()`

See `knowledge/how_to_create_electrical_components.md` for details on solver roles.

### 3. State transitions go in `commit()`, not `execute()`

For stateful components (Relay, Switch, AZS, etc.):

- Read inputs in `execute()` — from previous frame's committed state
- Stage state changes in `execute()` or compute outputs
- Apply state changes in `commit()` — visible next frame

This is the one-frame delay semantics of push propagation.

Bad (state change in execute):

```cpp
void execute(SimulationState& st, double dt) {
    // This runs every frame, can cause feedback loops
    if (st.values[ctrl] > threshold) {
        closed = true;  // BAD: direct state mutation
    }
}
```

Good (state change in commit):

```cpp
void execute(SimulationState& st, double /*dt*/) {
    // Read state, compute outputs
    float g = closed ? on_conductance : off_conductance;
    st.values[out] = st.values[in] * g;
}

void commit(SimulationState& st, double /*dt*/) {
    // Stage state change for next frame
    if (st.values[ctrl] > threshold) {
        next_closed = true;
    }
    closed = next_closed;
}
```

### 4. Use helpers for mathematical operations

```cpp
// Safe division
float safe_r = std::max(r_internal, 1e-9f);

// Safe volume
float safe_volume = std::max(gas_volume, 0.01);

// Clamp outputs
float out = std::clamp(value, 0.0f, 1.0f);
```

### 5. Keep defaults moderate

Avoid dangerous defaults:
- extremely high conductance (near-short)
- near-zero resistance
- huge gains
- idealized no-loss behavior unless needed

### 6. Separate physical unknowns from convenience outputs

Some outputs are just measurements or derived signals:
- `i_out` on `CurrentSense`
- logic outputs
- display values

These should be computed simply from available solved state.

## Recommended workflow for new components

> **Zero factory code required.** The component factory (`build_factory.cpp`) is auto-generated from `param_schema`. Adding a new component needs only blueprint + header + codegen rerun.

### Step 0. Checklist before starting

- Can the behavior be expressed as a **composite** of existing primitives? → Don't create a new C++ class.
- If a C++ class is needed, decide: **solver-owned electrical** (uses subsolver) or **push component** (runs in scheduler).

### Step 1. Create the blueprint

Place in `library/<category>/MyComponent.blueprint`:

```json
{
  "version": "3.0",
  "id": "MyComponent",
  "display_name": "My Component",
  "scheduler_source": false,
  "solver_owned_electrical": false,
  "interface": [
    {"name": "in", "domain": 2, "direction": 0, "type": "Signal", "source_writer": false},
    {"name": "out", "domain": 2, "direction": 1, "type": "Signal", "source_writer": false}
  ],
  "cpp_class": true,
  "domains": ["Logical"],
  "param_defaults": {"gain": "1.0"},
  "param_schema": {
    "gain": {"type": "float"}
  }
}
```

**Key `param_schema` fields:**
- `"type"`: `"float"` | `"int"` | `"bool"` | `"string"` | `"table"`
- `"field"`: override C++ member name (if param name ≠ field name, e.g. `"initial_position"` → `"field": "selected"`)
- `"visual_only": true`: param is editor-only, never reaches factory
- `"arena_field_offset"` / `"arena_field_size"`: for `"type": "table"` only (LUT pattern)

### Step 2. Create the C++ header

Place in `src/core/solvers/jit/components/my_component.h`:

```cpp
#pragma once
#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"

template <typename Provider = JitProvider>
class MyComponent {
public:
    static constexpr Domain domain = Domain::Logical;
    Provider provider;
    float gain = 1.0f;

    void execute(SimulationState& st, double /*dt*/) {
        float in = st.values[provider.get(PortNames::in)];
        st.values[provider.get(PortNames::out)] = in * gain;
    }
    void commit(SimulationState& st, double dt) {}
    void pre_load() {}  // Always define — called by generated factory
};
```

### Step 3. Register in all.h and component_kind.h

Add `#include "my_component.h"` to `src/core/solvers/jit/components/all.h`.

Add `MyComponent,` to `ComponentKind` enum in `src/core/model/component_kind.h` (alphabetical position). Also add entries to `parse_component_kind()` and `component_kind_classname()`.

### Step 4. Run codegen + rebuild

```bash
./build/tools/update_port_registry
cmake --build build -j$(nproc)
cd build && ctest
```

The codegen emits:
- `port_registry.h` — ComponentVariant entry + port metadata
- `port_names.h` — PortNames enum entries
- `build_factory.cpp` — construction switch case with param assignment + registration

**No factory code changes needed.** The codegen derives everything from `param_schema`.

### Registration rules (automatic)

The codegen emits scheduler registration based on blueprint flags:

| `scheduler_source` | `solver_owned_electrical` | Registration |
|---|---|---|
| any | `true` | **None** (electrical subsolver manages it) |
| `true` | `false` | `add_source` |
| `false` | `false` | `add_consumer` |

### Pre-load for computed state

If the component needs to derive computed fields from params (clamping, precomputed reciprocals, etc.), put logic in `pre_load()`:

```cpp
void pre_load() {
    inv_gain = 1.0f / std::max(gain, 1e-6f);
    positions = std::clamp(positions, 2, MAX_POSITIONS);
}
```

The generated factory always calls `comp.pre_load()` after param assignment.

## Red flags

If a component does any of these, inspect carefully:

- updates internal accumulators inside `execute()` without using `commit()`
- writes to solver-owned signals (those in electrical_plan)
- uses huge default conductance to simulate ideal behavior
- divides by a runtime value without a floor
- changes topology-like behavior inside execute phase

## Quick checklist

- Use `commit()` for state transitions, not `execute()`
- Electrical components should have solver roles, not use execute for stamping
- Clamp dangerous math operations
- Choose moderate defaults
- Test disconnected and shorted topologies

## Blueprint authoring conventions

### Node naming and visual hints

All nodes in a blueprint's `nodes[]` array must have a non-empty `name` field to avoid editor bugs. Use readable, stable names (can mirror the `id` field if appropriate).

For **Value nodes** in authored system blueprints:
- Add `render_hint: "ref"` to enable consistent visual rendering
- This ensures constant values display inline like in the `closed_circuit.blueprint` reference design
- Example: a node with `"value": 28.0` and `"render_hint": "ref"` will render visually identical to other constants in the system

## Bottom line

The best component is not the most physically detailed one.

It is the one that:

- produces believable outputs
- stays stable in bad editor-authored schematics
- behaves predictably with modest solver precision
- is easy to reason about and test

## Design philosophy

### Minimize C++ components

The simulator core should know as few `cpp_class: true` component types as possible. The goal is a small set of general-purpose **primitives** (Battery, Resistor, Switch, Comparator, PID, LUT, etc.) that can be composed into arbitrarily complex subsystems via **blueprint composites** (`cpp_class: false`).

A system like `12SAM28` (lead-acid battery with internal resistance, SOC tracking, and temperature compensation) should be a pure composite built from ControlledVoltageSource, CurrentSense, Accumulator, LUT, Multiply, Splitter, Clamp, Normalize, and Value — not a dedicated C++ class.

Why:

- fewer C++ types = less code to maintain, test, and compile
- composites are editable in the visual editor without recompiling
- the simulator core stays generic and does not accumulate domain-specific knowledge
- new subsystems can be authored by non-programmers

Rule of thumb: if the behavior can be expressed as a network of existing primitives with wires, it should be a composite.

### Blueprint composite design patterns

#### Port naming after expansion

When a composite blueprint (e.g., `12SAM28`) is instantiated as device `sb`:

- Internal devices get prefixed: `src` → `sb:src`
- BlueprintInput/Output bridge nodes become: `v_out` → `sb:v_out`
- Bridge ports: `sb:v_out.ext` (parent-facing) and `sb:v_out.port` (internal-facing)
- These are unified by union-find into a single signal
- Parent connections are rewritten: `sb.v_out` → `sb:v_out.ext`

**Querying composite ports:** Use `get_port_value("sb", "v_out")` — this automatically tries both `sb.v_out` (flat) and `sb:v_out.ext` (composite) formats.

#### Fanout with Splitter

Blueprint wiring is one-to-one: each port can have exactly one wire. To fan out a signal, use a `Splitter` component:

```
accum.out → split.i
split.o1 → clamp.in    (branch 1)
split.o2 → normalize.in (branch 2)
```

Do NOT connect two wires to the same output port — the parser will warn and behavior is undefined.

#### One-frame delay in feedback loops

Composites with feedback loops (e.g., LUT → CVS cmd → electrical solve → CurrentSense → Accumulator → Normalize → LUT) exhibit one-frame delay because:

1. `update_dynamic_sources` (phase 1) reads CVS cmd from signal array
2. `solve_electrical` (phase 2) uses the stamped CVS voltage
3. `scheduler.step` (phase 3) runs all logical components, including LUT which writes new cmd

So the CVS sees cmd from the **previous** frame's LUT output. On frame 1, cmd starts at 0 (LUT hasn't run yet). On frame 2, cmd reflects the LUT output from frame 1.

**Testing implication:** Tests for composites with feedback loops should use 2+ warmup steps before measuring steady-state values. Example from `test_12sam28.cpp`:

```cpp
sim.step(1.0 / 60.0);  // Frame 1: LUT writes cmd, CVS sees 0
sim.step(1.0 / 60.0);  // Frame 2: CVS sees LUT output from frame 1
// Now measure steady-state values
```

#### 12SAM28 reference design

The `12SAM28.blueprint` (`library/systems/12SAM28.blueprint`) is the reference composite for a battery with:

- **ControlledVoltageSource** (`src`): Thevenin source with r_internal=0.05Ω
- **CurrentSense** (`csense`): measures discharge current
- **Multiply** (`rate`): current × (-1/3600) → Ah/s discharge rate
- **Accumulator** (`accum`): integrates rate, initial=28 Ah
- **Splitter chain**: fans out charge to clamp + normalize paths
- **Clamp** (`clamp_charge`): clamps charge to [0, 28]
- **Normalize** (`norm_soc`): charge/28 → SOC [0,1]
- **LUT** (`ocv`): SOC → OCV (0→21V, 0.5→24V, 1.0→25.2V)
- **Feedback**: LUT output → CVS cmd (one-frame delay)

### Prefer ports and values over params

Params (`param_defaults`, `param_schema`) should only be used where truly necessary:

- LUT table data (arrays that don't map to a single signal)
- physical constants that never change at runtime (e.g., number of poles in a generator)
- configuration that affects topology (e.g., which mode a component operates in)

Everything else should be a **port**. Ports are signals — they flow through wires, they're visible in the editor, they can be driven by other components, and they participate in the scheduler's topological sort.

Bad (param for a tunable threshold):

```json
{
    "param_defaults": { "threshold": "5.0" }
}
```

Good (port for a tunable threshold, with a default via initial_value on the wire or a Value component):

```json
{
    "interface": [
        { "name": "threshold", "direction": 0, "type": "Any" }
    ]
}
```

Why:

- params are invisible to the scheduler — changing them doesn't propagate
- params can't be driven by other components at runtime
- ports make data flow explicit and debuggable in the editor
- a blueprint author can always hardwire a port to a constant Value node if it should be fixed

### Avoid Divide at all costs

Division is expensive and dangerous (divide-by-zero). Prefer:

- precomputed reciprocals (`inv_r = 1.0f / r` at init time, then multiply by `inv_r`)
- reciprocal ports (expose `inv_inertia` instead of `inertia`)
- math hacks (e.g., `x * (1.0f / constant)` instead of `x / constant`)
- the `Multiply` primitive with a reciprocal input, instead of `Divide`

The `Divide` component exists but should be a last resort. Blueprint authors should be guided toward `Multiply` + reciprocal patterns.

### Never pollute global things with concrete component details

The editor's global data structures (NodeContent, NodeContentType, etc.) are shared across ALL component types. **Never add component-specific fields** to these globals.

Instead:
- Reuse existing generic fields (`value`, `state`, `min`, `max`, etc.)
- Create new generic content types if needed (e.g., `Indicator` for circle-based display)
- Handle component-specific visualization in the component's update logic, not in global structs

Bad (polluting NodeContent):
```cpp
struct NodeContent {
    ...
    float indicator_brightness = 0.0f;  // BAD: only used by IndicatorLight
    bool azs_tripped = false;           // BAD: only used by AZS
};
```

Good (using generic fields):
```cpp
// IndicatorLight uses content.value (0-1 normalized brightness)
// AZS uses content.state + content.tripped (already generic)
```
