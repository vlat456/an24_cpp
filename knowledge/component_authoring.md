# Component Authoring Guide

This guide is for writing components that behave well in the current solver.

Relevant code:

- `/Users/vladimir/an24_cpp/src/jit_solver/components/all.h`
- `/Users/vladimir/an24_cpp/src/jit_solver/components/all.cpp`
- `/Users/vladimir/an24_cpp/src/jit_solver/components/provider.h`
- `/Users/vladimir/an24_cpp/src/jit_solver/state.h`
- `/Users/vladimir/an24_cpp/src/jit_solver/state.cpp`

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

### Step 1. Determine the component role

For electrical components, decide:
- **Solver-owned**: contributes to electrical subsolver (Battery, Generator, Resistor, etc.)
- **Push component**: runs in scheduler, reads solved values

See `knowledge/how_to_create_electrical_components.md`.

### Step 2. Implement push component pattern

```cpp
template <typename Provider>
void MyComponent<Provider>::execute(SimulationState& st, double /*dt*/) {
    float in = st.values[provider.get(PortNames::v_in)];
    st.values[provider.get(PortNames::v_out)] = in * param;
}

template <typename Provider>
void MyComponent<Provider>::commit(SimulationState& st, double dt) {
    if (state_transition_condition) {
        next_state = new_state;
    }
    state = next_state;
}
```

### Step 3. Add state machine for stateful components

```cpp
void MyComponent::execute(SimulationState& st, double /*dt*/) {
    // Read inputs from committed state
    float ctrl = st.values[provider.get(PortNames::ctrl)];
    
    // Compute outputs
    float g = closed ? on_conductance : off_conductance;
    st.values[provider.get(PortNames::v_out)] = g;
}

void MyComponent::commit(SimulationState& st, double /*dt*/) {
    // Stage transition
    if (st.values[provider.get(PortNames::ctrl)] > threshold) {
        next_closed = true;
    } else {
        next_closed = false;
    }
    // Apply for next frame
    closed = next_closed;
}
```

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

## Bottom line

The best component is not the most physically detailed one.

It is the one that:

- produces believable outputs
- stays stable in bad editor-authored schematics
- behaves predictably with modest solver precision
- is easy to reason about and test
