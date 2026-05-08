# Multi-Domain Nodal Solver

The simulation uses a single domain-agnostic nodal solver for electrical, hydraulic, and pneumatic networks.

## Overview

Traditional circuit simulation solves for voltage and current. The An-24 solver generalizes this to any domain using the three-element nodal model:
- **FixedNode** — clamps potential (ground, reference pressure)
- **Source** — drives potential (battery, pump, compressor)
- **Branch** — conducts flow between nodes (resistor, valve, orifice)

## Domains

| Domain | Potential | Flow | Example Elements |
|--------|-----------|------|------------------|
| Electrical | Voltage (V) | Current (A) | Battery, Resistor, Switch |
| Hydraulic | Pressure (Pa) | Flow (m³/s) | Pump, Valve, Orifice |
| Pneumatic | Pressure (Pa) | Flow (m³/s) | Compressor, Valve, Line |

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Simulator::step(dt)                                          │
│  ├─ solve_nodal(electrical_plan, element_value_a, state,    │
│  │              electrical_rt, dt)                           │
│  ├─ solve_nodal(hydraulic_plan, element_value_a, state,     │
│  │              hydraulic_rt, dt)  [if present]              │
│  ├─ solve_nodal(pneumatic_plan, element_value_a, state,     │
│  │              pneumatic_rt, dt)  [if present]              │
│  └─ scheduler.step(state, dt)  // push domains              │
└─────────────────────────────────────────────────────────────┘
```

## SimulationState Runtime Pointers

```cpp
struct SimulationState {
    std::vector<float> values;
    // ...
    NodalRuntimeState* electrical_rt = nullptr;  // Set during electrical solve
    NodalRuntimeState* hydraulic_rt = nullptr;   // Set during hydraulic solve
    NodalRuntimeState* pneumatic_rt = nullptr;   // Set during pneumatic solve
};
```

Only one domain's runtime state is active at a time during its solve phase.

## NodalBuildPlan

Per-domain build plan created at simulation build time:
```cpp
struct NodalBuildPlan {
    struct Island {
        std::vector<FixedNode> fixed_nodes;
        std::vector<Source> sources;
        std::vector<Branch> branches;
    };
    std::vector<Island> islands;
};
```

File: `src/core/solvers/common/nodal_types.h`

## NodalRuntimeState

Per-domain runtime scratch buffers:
```cpp
struct NodalRuntimeState {
    std::vector<float> potentials;   // Solved node potentials
    std::vector<float> branch_flows; // Solved branch flows
    // Solver workspace (matrices, LU factors, etc.)
};
```

## solve_nodal API

```cpp
/// Full version — JIT Simulator uses this with explicit element_value_a.
void solve_nodal(
    const NodalBuildPlan& plan,
    const std::vector<float>& element_value_a,  // Dynamic source values
    SimulationState& st,
    NodalRuntimeState& rt,
    double dt
) noexcept;

/// Self-contained overload — initializes element_value_a from plan defaults.
/// Used by AOT codegen and standalone tests.
void solve_nodal(
    const NodalBuildPlan& plan,
    SimulationState& st,
    NodalRuntimeState& rt,
    double dt
) noexcept;
```

File: `src/core/solvers/jit/subsolvers/nodal_subsolver.h`

## Solver Roles

Components declare their solver participation via `solver_role` metadata in library blueprints:

```json
{
  "solver_role": {
    "kind": "ConductanceBranch",
    "ports": { "a": "v_in", "b": "v_out" },
    "params": { "g": "conductance" }
  }
}
```

Supported kinds:
- `FixedVoltageNode` / `FixedPressureNode` — clamps potential
- `TheveninSource` — voltage/pressure source with internal resistance
- `ConductanceBranch` — linear conductance between two nodes

## Reading Solved State

Components access solved values via the active runtime state pointer:

```cpp
void MyComponent::execute(SimulationState& st, double /*dt*/) {
    // Electrical domain
    if (st.electrical_rt != nullptr) {
        float current = get_branch_current(*st.electrical_rt, electrical_handle);
        st.values[provider.get(PortNames::i_out)] = current;
    }

    // Hydraulic domain
    if (st.hydraulic_rt != nullptr) {
        float flow = get_branch_current(*st.hydraulic_rt, hydraulic_handle);
        st.values[provider.get(PortNames::flow_out)] = flow;
    }
}
```

Note: `get_branch_current()` is domain-agnostic — it returns "flow" through the branch, whether that's electrical current or fluid flow.

## Hydraulic Components

Library blueprints:
- `library/hydraulic/HydraulicPump.blueprint`
- `library/hydraulic/HydraulicValve.blueprint`
- `library/hydraulic/HydraulicRef.blueprint`

## Pneumatic Components

Library blueprints:
- `library/pneumatic/PneumaticCompressor.blueprint`
- `library/pneumatic/PneumaticValve.blueprint`
- `library/pneumatic/PneumaticRef.blueprint`

## Files

| File | Purpose |
|------|---------|
| `src/core/solvers/jit/subsolvers/nodal_subsolver.h` | Domain-agnostic solver |
| `src/core/solvers/common/nodal_types.h` | NodalBuildPlan, NodalRuntimeState |
| `src/core/solvers/common/nodal_patch_ops.h` | Patch operations for dynamic sources |
| `src/core/solvers/common/nodal_patch_types.h` | Patch type definitions |
| `src/core/solvers/common/build_algorithms.h` | Build algorithms (shared JIT/AOT) |
| `src/core/domain_types.h` | Domain enum |
