# How To Create Electrical Components

Practical guide for creating electrical (and hydraulic/pneumatic) components in the current architecture.

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

## Supported Nodal Solver Roles

Current role kinds (domain-agnostic):

- `FixedNode` — clamps node potential (voltage/pressure)
- `TheveninSource` — source with internal resistance
- `ConductanceBranch` — linear conductance between two nodes

If your component cannot map to these, it needs architecture extension.

---

## Step-by-Step: Primitive Component

### 1) Add library blueprint definition

Create `library/<domain>/MyComponent.blueprint`.

Required fields:

- `id`
- `scheduler_source`
- `interface`
- `domains`
- `cpp_class`
- `param_defaults`
- `solver_role`

Example (conductance branch, electrical):

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

Example (hydraulic valve):

```json
{
  "version": "3.0",
  "id": "HydraulicValve",
  "display_name": "Hydraulic Valve",
  "scheduler_source": false,
  "interface": [
    { "name": "p_in",  "domain": 8, "direction": 0, "type": "Pressure" },
    { "name": "p_out", "domain": 8, "direction": 1, "type": "Pressure" },
    { "name": "ctrl",  "domain": 2, "direction": 0, "type": "Signal" }
  ],
  "cpp_class": true,
  "domains": ["Hydraulic"],
  "param_defaults": {
    "max_conductance": "1.0"
  },
  "solver_role": {
    "kind": "ConductanceBranch",
    "ports": { "a": "p_in", "b": "p_out" },
    "params": { "g": "max_conductance" }
  }
}
```

### 2) Add C++ component files

Add header/source under `src/core/solvers/jit/components/`.

For solver-owned primitives, `execute`/`commit` are usually no-op:

```cpp
template <typename Provider>
void ElectricalConductanceLike<Provider>::execute(SimulationState&, double) {}

template <typename Provider>
void ElectricalConductanceLike<Provider>::commit(SimulationState&, double) {}
```

If you need derived outputs (observer behavior), compute those from solved values in `execute`:

```cpp
template <typename Provider>
void CurrentSense<Provider>::execute(SimulationState& st, double) {
    if (st.electrical_rt != nullptr) {
        float i = get_branch_current(*st.electrical_rt, handle_);
        st.values[provider.get(PortNames::i_out)] = i;
    }
}
```

### 3) Register in component includes/variant

Update:

- `src/core/solvers/jit/components/all.h`
- relevant variant registration paths in `jit_solver` build logic

### 4) Regenerate port registry

Run:

```bash
cmake --build build --target regenerate_port_registry
```

This updates:
- `src/core/solvers/jit/build_factory.cpp`
- `src/core/model/component_kind.h`

### 5) Add library index entry

Ensure `library/library_index.json` contains an entry for the new component.

### 6) Write tests

Add tests in `tests/` following existing patterns:

```cpp
TEST(MyComponentTest, BasicBehavior) {
    auto comp = make_my_component();
    auto st = make_state();
    comp.execute(st, 1.0/60.0);
    EXPECT_NEAR(st.values[provider.get(PortNames::out)], expected, 0.001f);
}
```

---

## Domain Values Reference

| Domain | JSON String | Numeric Value |
|--------|-------------|---------------|
| Electrical | `"Electrical"` | 1 |
| Logical | `"Logical"` | 2 |
| Mechanical | `"Mechanical"` | 4 |
| Hydraulic | `"Hydraulic"` | 8 |
| Thermal | `"Thermal"` | 16 |
| Pneumatic | `"Pneumatic"` | 32 |

---

## Files

| File | Purpose |
|------|---------|
| `src/core/solvers/jit/components/all.h` | Component includes |
| `src/core/solvers/jit/build_factory.cpp` | AUTO-GENERATED factory |
| `src/core/model/component_kind.h` | AUTO-GENERATED ComponentKind enum |
| `src/core/solvers/common/build_algorithms.h` | Build algorithms |
| `src/core/solvers/common/element_extraction.h` | Element extraction |
| `src/core/solvers/common/nodal_types.h` | Nodal types |
| `src/core/domain_types.h` | Domain enum |
| `library/library_index.json` | Library index |
| `src/io/json/component_registry_json_loader.h` | JSON loader |
