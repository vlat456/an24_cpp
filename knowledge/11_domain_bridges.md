# Electrical-Logical Bridge Components

## Why These Components Exist

Logical blocks (`PID`, `LUT`, `Add`, `Multiply`, `Clamp`, `FastTMO`, etc.) operate on scalar signals.
Electrical blocks (`Battery`, `Generator`, `Resistor`, `Bus`, `DMR400`, etc.) participate in circuit solving by stamping conductance and current into the electrical network.

These two worlds must not be connected directly.

The bridge components provide the legal conversion layer between:

- electrical voltage values observed from the solved circuit
- logical control signals produced by regulators and control chains
- electrical sources or loads driven from those control signals

This is especially important for blueprint implementations of `GS24`, `RUG82`, and similar regulator/generator systems.

## Core Rule

Never wire a logical controller output directly into an electrical node and never feed an electrical node directly into logical math unless a bridge component is in between.

Correct pattern:

```text
Electrical node -> VoltageSense -> logical control chain -> ControlledVoltageSource / ControlledCurrentSource / VariableConductance -> Electrical network
```

## Execution-Phase Contract

Bridge behavior is phase-explicit:

- `VoltageSense` and `Voltmeter` run in `observe_electrical` (after first SOR).
- Logical controllers (`P/PI/PD/PID`, math/LUT/filter blocks) run in `solve_logical`.
- `ControlledVoltageSource`, `ControlledCurrentSource`, and `VariableConductance` stamp in `electrical_actuator`.

This gives same-step closed-loop response in one outer `step(dt)`:

1. electrical settles (passive SOR),
2. measurements are observed,
3. control is computed,
4. actuators stamp,
5. second SOR applies the command in the same step.

Execution participation is declared in component blueprint metadata (`execution` block), consumed identically by JIT and AOT.

## Bridge Components

### `VoltageSense`

Purpose:
- Converts electrical voltage difference into a logical/scalar signal.

Ports:
- `v_in` - measured positive node
- `v_ref` - reference node
- `out` - logical output signal

Behavior:
- Reads `(v_in - v_ref)` after the electrical solver converges.
- Writes `out = (v_in - v_ref) * gain + offset`.
- Does not stamp conductance and does not load the circuit.

Use cases:
- Measure generator output voltage for a regulator loop.
- Convert bus voltage into a feedback signal for `PID`.
- Sense differential voltage without disturbing the network.

Typical usage:

```text
Generator bus -> VoltageSense(v_in)
Ground/ref bus -> VoltageSense(v_ref)
VoltageSense(out) -> PID(feedback)
```

### `ControlledVoltageSource`

Purpose:
- Converts a logical/scalar command into an electrical voltage source.

Ports:
- `cmd` - logical control command
- `v_pos` - positive electrical node
- `v_neg` - negative/reference electrical node

Behavior:
- Computes `v_source = clamp(cmd * gain + offset, min_v, max_v)`.
- Stamps a Thevenin/Norton-equivalent source into the electrical solve.
- `r_internal` controls source stiffness.

Use cases:
- Build a controllable generator output from a regulator command.
- Model an electrically driven source whose voltage is set by logic.

Typical usage:

```text
PID/LUT output -> ControlledVoltageSource(cmd)
ControlledVoltageSource(v_pos) -> generator output bus
ControlledVoltageSource(v_neg) -> reference/ground bus
```

### `ControlledCurrentSource`

Purpose:
- Converts a logical/scalar command into injected electrical current.

Ports:
- `cmd` - logical control command
- `v_pos` - current injected into this node
- `v_neg` - current removed from this node

Behavior:
- Computes `i_source = clamp(cmd * gain, min_i, max_i)`.
- Injects current as a Norton source.
- `g_shunt` adds a small parallel conductance to keep the solve well-conditioned.

Use cases:
- Model commanded excitation current.
- Model current-controlled actuators or regulators.
- Prefer this when the physical quantity is current, not voltage.

### `VariableConductance`

Purpose:
- Converts a logical/scalar command into a variable electrical conductance.

Ports:
- `cmd` - logical command, usually expected in `[0..1]`
- `v_in` - one electrical node
- `v_out` - the other electrical node

Behavior:
- Computes `g = lerp(g_min, g_max, clamp(cmd, 0, 1))`.
- Stamps a resistor-like two-port into the electrical network.
- Low command -> weak conductance, high command -> strong conductance.

Use cases:
- Carbon pile behavior.
- Variable field/load resistance.
- Commanded shunt or bleed path.

Typical usage:

```text
Regulator command -> VariableConductance(cmd)
VariableConductance(v_in) -> source node
VariableConductance(v_out) -> sink/reference node
```

## Recommended Regulator Topology

For a blueprint implementation of a voltage regulator such as `RUG82`:

```text
electrical bus voltage
  -> VoltageSense
  -> PID / LUT / filters / clamps
  -> ControlledVoltageSource or VariableConductance
  -> electrical generator/load side
```

Interpretation:
- `VoltageSense` is the observer.
- `PID`, `LUT`, `Clamp`, `FastTMO`, `Add`, `Multiply` are the control law.
- `ControlledVoltageSource`, `ControlledCurrentSource`, or `VariableConductance` is the actuator.

Pick the actuator by physical meaning:
- target is commanded terminal voltage -> `ControlledVoltageSource`
- target is commanded excitation current -> `ControlledCurrentSource`
- target is commanded effective resistance / load / shunt -> `VariableConductance`

## Notes For `GSC.blueprint`

- The old graph mixed logical nodes with electrical values directly.
- The new bridge nodes should be the only crossing points between the control chain and the electrical network.
- If one command must feed multiple consumers, use `Splitter` explicitly.
- Keep connections one-to-one; do not fan out one output into several wires directly.

Example pattern:

```text
VoltageSense(out)
  -> Splitter(i)
  -> PID(feedback)
  -> Voltmeter/indicator path via splitter outputs if needed
```

## Practical Guidelines

- Use `VoltageSense` for measurement only; it should never drive the circuit.
- Use `ControlledVoltageSource` when you want the logic chain to create a bus/reference voltage.
- Use `ControlledCurrentSource` when command should represent source current.
- Use `VariableConductance` when command should represent loading, shunting, or field strength through resistance/conductance.
- Add `Clamp` before an actuator if the control law must stay within a safe physical range.
- Add `FastTMO` or similar filtering before the actuator if the regulator should not react instantly.

## Mental Model

Think of the bridge components as transducers between two simulation domains:

- `VoltageSense`: electrical -> logical
- `ControlledVoltageSource`: logical -> electrical voltage actuation
- `ControlledCurrentSource`: logical -> electrical current actuation
- `VariableConductance`: logical -> electrical impedance actuation

That separation keeps the blueprint physically meaningful and avoids illegal domain coupling.
