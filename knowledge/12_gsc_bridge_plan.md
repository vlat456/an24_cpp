# GSC Bridge Wiring Plan

## Goal

Rebuild `GSC.blueprint` so the regulator and generator interaction uses explicit electrical-logical bridge components instead of direct mixed-domain wiring.

This plan assumes:

- no direct logical <-> electrical connections
- all fan-out is explicit via `Splitter`
- `GSC.blueprint` is implemented from base components, not C++ `GS24` / `RUG82`

## High-Level Architecture

Split the system into 3 layers:

1. Electrical plant
- generator output node
- output bus / load path
- field/load side electrical actuator

2. Measurement bridge
- observe electrical voltage with `VoltageSense`

3. Logical regulator
- setpoint, error shaping, PID, LUT, filters, clamps
- actuator command output

## Recommended Signal Flow

```text
generator electrical node
  -> VoltageSense
  -> Splitter
  -> PID/LUT/filter/clamp chain
  -> Splitter
  -> electrical actuator bridge
  -> generator electrical network
```

## Recommended Node Roles

### Electrical side

- `Bus` for the generator output / main observed node
- `RefNode` for electrical reference
- `Voltmeter` only for display
- `DMR400` or other downstream electrical switching stays entirely electrical

### Measurement bridge

- `VoltageSense`
  - `v_in` = generator or regulated bus node
  - `v_ref` = reference bus / ground
  - `out` = logical feedback signal

### Logical regulator chain

- `RefNode` or constant node for target voltage command in logical form
- `PID` for voltage error correction
- `LUT` for shaping non-linear carbon pile / excitation response
- `FastTMO` for smoothing actuator motion
- `Clamp` for safe min/max command range
- `Add` / `Multiply` only for explicit bias/gain shaping
- `Splitter` anywhere one logical result must feed more than one consumer

### Electrical actuator side

Choose one actuator based on intended physical meaning:

- `ControlledVoltageSource`
  - use when control law should directly create a generator terminal voltage

- `ControlledCurrentSource`
  - use when control law should inject excitation/source current

- `VariableConductance`
  - use when control law should model carbon pile, shunt, field resistance, or variable load

## Suggested `RUG82` Blueprint Structure

If `RUG82` remains a nested blueprint, its internals should look like this:

```text
BlueprintInput(v_gen electrical)
  -> VoltageSense(v_in)
RefNode(0V electrical)
  -> VoltageSense(v_ref)

VoltageSense(out)
  -> PID(feedback)

Setpoint constant
  -> PID(setpoint)

PID(output)
  -> LUT(input)
  -> FastTMO(in)
  -> Clamp(in)
  -> BlueprintOutput(k_mod logical)
```

Important:
- `v_gen` must stay electrical until it enters `VoltageSense`
- `k_mod` should be treated as a logical control signal after the control chain
- if `k_mod` drives multiple places, add a `Splitter`

## Suggested `GS24` / `GSC` Blueprint Structure

At top level, use the regulator output only through a bridge actuator:

```text
regulated electrical bus
  -> VoltageSense
  -> regulator chain
  -> Clamp
  -> Splitter
  -> ControlledVoltageSource / ControlledCurrentSource / VariableConductance
  -> generator electrical node
```

Recommended concrete mapping:

### Option A: direct voltage regulation

Use when the command should represent desired generator voltage.

```text
Clamp(out)
  -> ControlledVoltageSource(cmd)
ControlledVoltageSource(v_pos)
  -> generator output bus
ControlledVoltageSource(v_neg)
  -> reference bus
```

### Option B: excitation current regulation

Use when the command should represent field/source current.

```text
Clamp(out)
  -> ControlledCurrentSource(cmd)
ControlledCurrentSource(v_pos)
  -> excitation node
ControlledCurrentSource(v_neg)
  -> reference bus
```

### Option C: carbon pile / variable shunt regulation

Use when the command should change effective conductance in the electrical network.

```text
Clamp(out)
  -> VariableConductance(cmd)
VariableConductance(v_in)
  -> controlled source node
VariableConductance(v_out)
  -> sink/reference node
```

## One-to-One Wiring Rule

For `GSC.blueprint` do not allow a source port to connect to multiple wires directly.

If a signal must fan out:

```text
source -> Splitter(i)
Splitter(o1) -> consumer_1
Splitter(o2) -> consumer_2
```

Apply this rule to:

- voltage feedback reused by PID and instruments
- regulator command reused by actuator and display/debug nodes
- any reused intermediate shaped signal

## Minimal Migration Steps For `GSC.blueprint`

1. Identify all places where an electrical voltage is fed into logical nodes directly.
2. Insert `VoltageSense` at each crossing.
3. Identify all places where logical output currently influences electrical behavior directly.
4. Replace those crossings with one of:
   - `ControlledVoltageSource`
   - `ControlledCurrentSource`
   - `VariableConductance`
5. Insert `Splitter` nodes for every fan-out.
6. Keep meters visual-only and not part of the control law unless explicitly sensed through a bridge.

## Practical Recommendation For First Pass

For the first working `GSC` conversion:

- use one `VoltageSense` for generator/bus feedback
- keep the existing PID/LUT/FastTMO/Clamp chain
- drive a single `ControlledVoltageSource` from the final clamped command
- add `Splitter` only where the feedback or command is reused

This gives the simplest valid electrical-logical separation and is the easiest version to debug.
