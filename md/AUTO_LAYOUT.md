# Auto-Layout: Editor Reconstruction Without `editor` Block

## Problem

The JSON format had two parallel data paths: simulator data (`devices` + `connections`) and editor visual metadata (`editor` block with node positions, sizes, wire routing points). Users editing blueprints by hand in a text editor had to maintain both sections, which is error-prone and violates single-source-of-truth.

## Solution

When `blueprint_from_json()` loads a JSON file **without** an `editor` block, it now automatically:

1. **Classifies nodes** by circuit role (Ground, Source, Bus, Load)
2. **Positions nodes** in a layered layout:
   - Sources (Battery, Generator) → left column
   - Buses → center column, auto-sized by connection count
   - Loads → right column
   - Ground (RefNode) → bottom center
3. **Routes wires** using the existing A\* orthogonal router with obstacle avoidance

## Changes

### persist.cpp

- **Bug fix**: Bus/Ref node sizes (40×40) were unconditionally overwritten by the default 120×80. Now each `NodeKind` gets its correct size.
- **`port_center()`**: Computes world-space port position from `Node` data (inputs on left edge, outputs on right, Bus bottom-center, Ref top-center).
- **`auto_layout()`**: Topological layered layout + A\* wire routing. Called from `blueprint_from_json()` when no `editor` key exists.

### test_persist.cpp

- `AutoLayout_NoEditorBlock`: Verifies 4-node circuit gets unique positions, correct Bus/Ref sizes, and auto-routed wires.
- `AutoLayout_CompositeTestFile`: Verifies the real `an24_composite_test.json` (9 devices, 10 connections) gets correct layout with sources left of buses.

## Usage

```json
{
  "devices": [
    {
      "name": "gnd",
      "classname": "RefNode",
      "ports": { "v": { "direction": "Out" } }
    },
    {
      "name": "bat1",
      "classname": "Battery",
      "ports": {
        "v_in": { "direction": "In" },
        "v_out": { "direction": "Out" }
      }
    }
  ],
  "connections": [{ "from": "gnd.v", "to": "bat1.v_in" }]
}
```

Loading this in the editor will auto-layout nodes and route wires. If the user saves from the editor, the `editor` block is written and used on subsequent loads.

## Tests

All 198 tests pass. Auto-layout for composite test (9 nodes, 10 wires) completes in ~1.3s.
