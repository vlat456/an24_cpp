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

    void solve_electrical(SimulationState& st, float dt);
    void finalize_step(SimulationState& st, float dt);
    void pre_load();
};
```

## Rules

### 1. `solve_*()` should stamp, not evolve memory

Do not update persistent internal state in `solve_electrical()`, `solve_hydraulic()`, etc.

Bad:

```cpp
void solve_electrical(SimulationState& st, float dt) {
    integrator_state += error * dt;
    st.across[provider.get(PortNames::out)] = integrator_state;
}
```

Good:

```cpp
void solve_electrical(SimulationState& st, float /*dt*/) {
    st.across[provider.get(PortNames::out)] = integrator_state;
}

void finalize_step(SimulationState& st, float dt) {
    integrator_state += error * dt;
}
```

Why:

- `solve_*()` participates in the relaxation process
- changing hidden state there makes behavior depend on solver iteration behavior
- `finalize_step()` runs once per frame and is the right place for memory/state transitions

### 2. Two-port physics must use two-port stamps

If the device passes current/flow between two nodes, use `stamp_two_port(...)`.

Good:

```cpp
stamp_two_port(
    st.conductance.data(),
    st.through.data(),
    st.across.data(),
    provider.get(PortNames::v_in),
    provider.get(PortNames::v_out),
    g
);
```

Do not fake it as two separate loads to ground.

### 3. One-port loads should use the helper

For resistive load to ground:

```cpp
stamp_one_port_ground(
    st.conductance.data(),
    st.through.data(),
    st.across.data(),
    provider.get(PortNames::input),
    conductance
);
```

### 4. Voltage sources should be stamped in Norton-compatible form

Prefer the existing helper/patterns instead of inventing a custom residual equation unless you really need it.

Important:

- clamp tiny internal resistance
- avoid exact ideal source behavior
- always keep a finite path for conditioning

### 5. Keep defaults moderate

Avoid dangerous defaults such as:

- extremely high conductance
- near-zero resistance
- huge gains
- idealized no-loss behavior unless absolutely needed

Examples:

- measurement devices should not default to near-short values unless there is a strong reason
- controlled sources should have reasonable internal resistance

### 6. Separate physical unknowns from convenience outputs mentally

Some outputs are just measurements or signals.

Examples:

- `i_out` on `CurrentSense`
- logic outputs
- display values

These should not destabilize the physical solve. Keep their computation simple and derived from already-available state.

### 7. Clamp nonlinear expressions

Use safe bounds for divisions and nonlinear laws.

Examples:

```cpp
float safe_r = std::max(r_internal, 1e-9f);
float safe_v = std::max(v_diff, 1e-3f);
float safe_volume = std::max(gas_volume, 0.01f);
```

### 8. Prefer bounded approximations

For sim use, a bounded approximation is usually better than a more realistic but explosive model.

Examples:

- clamp source output ranges
- clamp rates of change
- use finite conductance instead of ideal wire behavior

## Recommended workflow for new components

### Step 1. Start with the simplest stable model

Example resistive pass-through:

```cpp
template <typename Provider>
void MyPassThrough<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    stamp_two_port(
        st.conductance.data(),
        st.through.data(),
        st.across.data(),
        provider.get(PortNames::v_in),
        provider.get(PortNames::v_out),
        conductance
    );
}
```

### Step 2. Add state only in `finalize_step()`

Example relay-like behavior:

```cpp
template <typename Provider>
void MyRelay<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    float g = closed ? on_conductance : off_conductance;
    stamp_two_port(
        st.conductance.data(),
        st.through.data(),
        st.across.data(),
        provider.get(PortNames::v_in),
        provider.get(PortNames::v_out),
        g
    );
}

template <typename Provider>
void MyRelay<Provider>::finalize_step(SimulationState& st, float /*dt*/) {
    float ctrl = st.across[provider.get(PortNames::ctrl)];
    closed = ctrl > threshold;
}
```

### Step 3. Add measurements last

Example current measurement:

```cpp
float v_diff = st.across[provider.get(PortNames::v_in)]
             - st.across[provider.get(PortNames::v_out)];
st.across[provider.get(PortNames::i_out)] = v_diff * conductance;
```

## Red flags

If a component does any of these, inspect it carefully:

- updates internal accumulators inside `solve_*()`
- writes to unrelated ports as a shortcut
- stamps both sides of a through-device independently to ground
- uses huge default conductance to simulate ideal behavior
- divides by a runtime value without a floor
- changes topology-like behavior continuously inside the solve phase

## Good test cases

For each new component, add tests for:

1. nominal behavior
2. disconnected ports
3. short-circuit / near-short case
4. zero or near-zero parameter edge case
5. stability over many steps

Example cases:

- source -> component -> load
- source -> component -> ground
- one side disconnected
- extreme parameter clamps

## Quick checklist

- use helper stamps where possible
- keep `solve_*()` stateless except for stamping/reading
- move memory/state transitions to `finalize_step()`
- clamp dangerous math
- choose moderate defaults
- test disconnected and shorted topologies

## Bottom line

The best component is not the most physically detailed one.

It is the one that:

- produces believable outputs
- stays stable in bad editor-authored schematics
- behaves predictably with modest solver precision
- is easy to reason about and test
