# Push Propagation Migration Plan

## Status Update (2026-03-30)

This document includes historical migration notes. Current branch status is:

- Push runtime is the active execution model in JIT and AOT paths.
- Component runtime API is unified around `execute()` with optional `commit()` and `pre_load()`.
- Legacy `execution` metadata was removed from library blueprints and parser paths.
- Strict unknown/unconsumed parameter rejection is enabled in factory build paths.
- Stateful commit semantics are covered by push runtime regressions.

### Completed migration slices

- Core push scheduler and push state model migration.
- Component API simplification and staged state transitions for stateful components.
- Strict parameter consumption checks in `build_systems_dev()`.
- Test-pad fixture hardening for GS24/RU19A (explicit defaults + targeted regressions).
- Full-suite validation from proper build directories (`build/tests`, `build_fulltests`).

### Remaining work

- Push migration cutover work is complete for runtime strictness goals.
- Remaining items are post-migration architecture debt tracked in `knowledge/errors_TODO.md`.

---

## Context

Current legacy iterative solver has fundamental limitations for game-scale simulation:
- Conducts ~15000 iterations/frame (60Hz) to converge
- CurrentSense conductance must balance circuit influence vs node coupling
- Ideal ammeter/impedance measurements are impossible
- Complex tuning for nested feedback loops

For MSFS/FlightGear grade simulation, accuracy target is 0.1V — much lower than scientific simulation. Push propagation is proven in JSBSim (used 20+ years in FlightGear).

## Target Architecture

### What Changes
- Remove legacy iterative solver entirely
- Components execute in topological/explicit order
- No matrix assembly, no iterations per frame
- Simple frame-by-frame propagation

### What Stays the Same
- Component templates with Provider pattern
- Domain-based frequency scheduling (electrical 60Hz, mechanical 20Hz, etc.)
- Blueprint V2 data model
- JIT/AOT dual-mode execution
- Signal-based port connections

## Runtime API Simplification Decision

**Principle:** Keep component runtime API minimal — one execute entry point plus optional end-of-frame commit for stateful components.

### API Contract (Current Slice)
| Method | Purpose | Who has it |
|--------|---------|------------|
| `execute(st, dt)` | Per-frame computation | All components |
| `commit(st)` | Apply staged state transitions | Stateful components (Switch, Relay, HoldButton, AZS, LerpNode) |
| `pre_load()` | Initialization | Components with params |

### Legacy Taxonomy (Being Removed)
Domain-specific methods are legacy taxonomy:
- `solve_electrical()`, `solve_mechanical()`, `solve_hydraulic()`, `solve_thermal()`, `solve_logical()`

These may remain as private helpers during migration but are NOT part of the public API.

### Migration Status
- Phase 1 (DONE): Introduce `commit()` hook in scheduler, add to 7 stateful components
- Phase 2 (TODO): Move domain logic into `execute()` or private helpers
- Phase 3 (TODO): Remove public `solve_*` from component headers
- Phase 4 (TODO): AOT codegen update to use simplified API

### One-Frame Delay Semantics
Stateful components (Switch, Relay, etc.) detect input changes in `execute()` but apply them in `commit()`. This means:
- Input change detected at frame N
- State committed at end of frame N  
- New state visible to other components in frame N+1

This is intentional for push propagation: prevents combinatorial feedback loops.

---

## Core Concepts

### Push Propagation Model

```
Frame N (dt = 1/60):
  1. Components read inputs from connected nodes
  2. Components compute outputs based on their physics
  3. Components write outputs to their nodes
  4. Repeat for all components
```

No simultaneous equations. Each component is a pure function:
```
output = f(input, state, dt)
```

### Why This Works for Games

Flight simulation doesn't need physically accurate electrical networks:
- Components model device behavior, not physical networks
- "Ammeter reads ~5A" is sufficient, not exact KCL/KVL
- Acceptable that connecting load affects voltage slightly
- JSBSim uses this approach successfully for decades

### Component Execution Order

Two approaches:

**A. Topological Sort**
- Analyze blueprint graph
- Execute sources → consumers
- Handle feedback loops specially

**B. Explicit Phase Order** (recommended)
- Phase 1: Sources set voltages (CVS, Battery)
- Phase 2: Passive components compute (Resistor, Load)
- Phase 3: Sensors read (CurrentSense, Voltmeter)
- Phase 4: Controllers compute (PI, PWM)
- Phase 5: Actuators apply (Relay, Switch)
- Repeat for feedback convergence

For feedback loops (PI controller), do 1-4 iterations per frame.

## Implementation Phases

Note for work on branch `push_migration`: temporary full breakage is acceptable during migration, and obsolete legacy iterative-era tests/files may be deleted as part of the rewrite.

### Phase 1: Core Infrastructure

**1.1 Define Execution Phases**

```cpp
enum class ExecPhase {
    SOURCE_SET,      // Voltage sources, current sources
    PASSIVE_STAMP,   // Resistive loads, conductances
    SENSOR_READ,     // CurrentSense, Voltmeter
    CONTROLLER_COMPUTE, // PI, PID, LUT, filters
    ACTUATOR_COMMIT, // Relay, Switch state changes
};
```

**1.2 Remove legacy solver from SimulationState**

```cpp
struct SimulationState {
    std::vector<float> across;  // Potentials (V, P, T) - write by sources
    std::vector<float> through; // Flows (I, Q, H) - write by loads
    
    // REMOVE: conductance, inv_conductance
    // REMOVE: iteration functions
};
```

**1.3 Create PushScheduler**

```cpp
class PushScheduler {
    struct ComponentEntry {
        ComponentVariant* component;
        ExecPhase phase;
        std::vector<uint32_t> input_signals;
        std::vector<uint32_t> output_signals;
    };
    
    std::vector<ComponentEntry> entries;
    size_t iterations_per_frame = 4; // For feedback loops
    
    void step(SimulationState& state, float dt);
};
```

### Phase 2: Component Migration

**2.1 Source Components**

CVS (Controlled Voltage Source):
```cpp
void CVS::execute(SimulationState& st, float dt) {
    float control = st.across[control_idx];
    float v_out = base_voltage * control;
    st.across[output_idx] = v_out;
}
```

Battery:
```cpp
void Battery::execute(SimulationState& st, float dt) {
    st.across[positive_idx] = v_nominal;
    st.across[negative_idx] = 0.0f;
}
```

**2.2 Passive Components**

Resistor:
```cpp
void Resistor::execute(SimulationState& st, float dt) {
    float v = st.across[positive_idx] - st.across[negative_idx];
    float i = v / r;
    st.through[node1_idx] += i;
    st.through[node2_idx] -= i;
}
```

HighPowerLoad:
```cpp
void HighPowerLoad::execute(SimulationState& st, float dt) {
    float v = st.across[positive_idx] - st.across[negative_idx];
    float abs_v = std::abs(v) + 0.001f; // Avoid division by zero
    float p = power.load(); // or compute from duty
    float i = p / abs_v;
    st.through[positive_idx] += i;
    st.through[negative_idx] -= i;
}
```

**2.3 Sensor Components**

CurrentSense:
```cpp
void CurrentSense::execute(SimulationState& st, float dt) {
    float i = st.through[wire_idx]; // Read accumulated current
    st.across[output_idx] = i / sensitivity;
}
```

Voltmeter:
```cpp
void Voltmeter::execute(SimulationState& st, float dt) {
    float v = st.across[positive_idx] - st.across[negative_idx];
    st.across[output_idx] = v;
}
```

**2.4 Controller Components**

PI Controller:
```cpp
void PI::execute(SimulationState& st, float dt) {
    float setpoint = st.across[setpoint_idx];
    float measured = st.across[measured_idx];
    float error = setpoint - measured;
    
    integral += error * dt;
    integral = std::clamp(integral, -windup_limit, windup_limit);
    
    float output = kp * error + ki * integral;
    output = std::clamp(output, output_min, output_max);
    
    st.across[output_idx] = output;
}
```

### Phase 3: Feedback Loop Handling

For circuits with feedback (PI → CVS → load → sense → PI), run multiple iterations:

```cpp
void PushScheduler::step(SimulationState& st, float dt) {
    for (size_t iter = 0; iter < iterations_per_frame; ++iter) {
        // Phase 1: Sources
        for (auto& e : sources) e.component->execute(st, dt);
        
        // Phase 2: Passives
        for (auto& e : passives) e.component->execute(st, dt);
        
        // Phase 3: Sensors
        for (auto& e : sensors) e.component->execute(st, dt);
        
        // Phase 4: Controllers
        for (auto& e : controllers) e.component->execute(st, dt);
    }
    
    // Phase 5: Actuators (once, after convergence)
    for (auto& e : actuators) e.component->execute(st, dt);
}
```

Typical iterations needed: 2-4 for GSC.

### Phase 4: Blueprint Format Changes

Add explicit execution order hints:

```json
{
  "devices": {
    "generator": {
      "type": "CVS",
      "phase": "SOURCE_SET",
      "inputs": ["control"],
      "outputs": ["v_out"]
    },
    "load": {
      "type": "HighPowerLoad",
      "phase": "PASSIVE_STAMP",
      "inputs": ["v_in"],
      "outputs": []
    },
    "pi": {
      "type": "PI",
      "phase": "CONTROLLER_COMPUTE",
      "inputs": ["setpoint", "measured"],
      "outputs": ["control"]
    }
  }
}
```

Or infer from component type automatically, with override capability.

### Phase 5: Testing

**Unit tests per component:**
- PI: step response, integral windup, saturation
- CVS: voltage tracking, current limits
- HighPowerLoad: power calculation, voltage polarity
- CurrentSense: current measurement

**Integration test: GSC**

```cpp
TEST(GSC, Stabilization) {
    auto bp = parse_json(R"({...})");
    auto result = build_blueprint(bp);
    
    for (int i = 0; i < 120; ++i) { // 2 seconds
        scheduler.step(state, 1.0f/60.0f);
    }
    
    EXPECT_NEAR(state.across[bus_voltage], 28.5f, 0.1f);
}
```

**Performance baseline:**
- Current: 900k iterations/sec
- Target: <10k component executions/sec (60Hz × 100 components × 2 iterations)

## Comparison: Legacy Solver vs Push

| Aspect | Legacy Iterative | Push |
|--------|-----|------|
| Convergence | Iterative (15000 iter/frame) | Direct |
| Accuracy | Scientific | Game-grade (0.1V) |
| Circuit effects | Accurate (requires tuning) | Approximate |
| CurrentSense | Balances conductance | Reads through[] |
| Setup complexity | Matrix assembly | Component phases |
| Debugging | Convergence issues | Execution order issues |
| Parallelization | Limited | Embarrassingly parallel |

## Migration Risks

### Risk 1: Feedback Loop Instability

**Problem:** Without iterative convergence, feedback loops may not settle.

**Mitigation:**
- Run 2-4 iterations per frame
- Add damping to controllers
- Accept small steady-state error

### Risk 2: Execution Order Sensitivity

**Problem:** Wrong order causes wrong results.

**Mitigation:**
- Explicit phase ordering
- Unit tests for each component
- Integration tests for common patterns

### Risk 3: Breaking AOT Codegen

**Problem:** AOT generates direct C++ calls, assumes iterative solver.

**Mitigation:**
- Update codegen templates
- Test JIT/AOT parity with new scheduler
- AOT actually simpler without iteration — direct function calls

### Risk 4: Missing Features

**Problem:** Some behaviors relied on iterative solver (e.g., voltage division through multiple nodes).

**Mitigation:**
- Identify during testing
- Add explicit multi-hop propagation if needed
- Accept reduced accuracy

## Files to Change

### Core Changes

1. `src/jit_solver/state.h` - Remove iteration, simplify
2. `src/jit_solver/scheduler.h` - New PushScheduler
3. `src/jit_solver/jit_solver.h` - Use PushScheduler

### Component Changes

4. `src/jit_solver/components/all.cpp` - Update execute methods
5. `src/jit_solver/components/all.h` - Add phase annotations

### Blueprint Changes

6. `src/blueprint_v2/` - Add phase inference
7. `library/electrical/*.blueprint` - Add phase hints

### Test Changes

8. `tests/` - Update for new scheduler
9. `tests/gsc_tests.cpp` - New integration test

### Remove

10. `src/jit_solver/sor.cpp` - Delete (if exists)
11. `src/jit_solver/SOR_constants.h` - Does not exist

## Implementation Order

1. Create PushScheduler skeleton
2. Migrate one simple component (Resistor)
3. Verify execution order logic
4. Migrate source components
5. Migrate sensor components
6. Migrate controllers
7. Implement feedback loop iterations
8. Full GSC integration test
9. Remove iteration code
10. Performance verification

## Success Criteria

- GSC stabilizes at 28.5V within 2 seconds
- Accuracy within 0.1V
- CurrentSense reads correct current
- Load connection/disconnection works
- 10x faster than legacy solver (target: <100μs/frame)
- All existing tests pass
- JIT/AOT parity maintained

## References

- JSBSim Systems: https://wiki.flightgear.org/JSBSim_Systems
- Push vs Pull propagation in game engines
- Component-based game architecture patterns
