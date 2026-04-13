# Value Node Rendering and Interaction Analysis

## Current Model

Value-node interaction behavior is determined first by `render_hint`, then by whether the chosen node widget exposes semantic content interaction.

- `render_hint="ref"` creates `RefNodeWidget`
- default component rendering creates `NodeWidget`
- only `NodeWidget` can expose semantic content interactions

That means a Value-like node enters content-control states such as `DraggingKnob` only if it is authored as a normal `NodeWidget` and also carries an interactive `content_type`.

## Node Type Selection

`src/editor/visual/node/node_factory.h` selects the visual node class:

- `bus` -> `BusNodeWidget`
- `ref` -> `RefNodeWidget`
- `group` -> `GroupNodeWidget`
- `text` -> `TextNodeWidget`
- default -> `NodeWidget`

This is still the first decision point for whether a node can expose semantic content controls.

## Current Content Interaction Path

The old direct node-content interaction path is gone.

The active path is:

1. `NodeWidget` reserves a content region and caches content state
2. `NodeWidget::refresh_content_semantic_snapshot()` emits retained semantic render and hit objects in node-local coordinates
3. `CanvasInput::hit_test_semantic_content()` converts world pointer coordinates into node-local space and semantic-hit-tests that snapshot
4. `InteractionBinding.kind` drives the input state transition

Current semantic interaction kinds:

- `InteractionKind::Click` -> toggle
- `InteractionKind::DragScalar` -> slider drag
- `InteractionKind::DragDiscrete` -> knob drag

## Why a Value Node Might Enter `DraggingKnob`

If a Value-like node is authored incorrectly like this:

- missing `render_hint="ref"`
- interactive `content_type` such as `"Knob"`

then the factory creates `NodeWidget`, not `RefNodeWidget`, and the node can expose semantic knob interaction. Clicking its content region can therefore enter `InputState::DraggingKnob`.

If the same node is authored correctly as a ref/value node:

- `render_hint="ref"`

then it becomes `RefNodeWidget`, does not expose semantic content controls, and normal node dragging behavior remains intact.

## Practical Rule

Value nodes that should behave like ref/value references must keep `render_hint="ref"`.

If a node is intended to behave like a normal interactive component, it should use the default `NodeWidget` path and an explicit interactive `content_type`.

## Summary

The original issue is no longer about a specific widget class. In the current architecture it is:

- `RefNodeWidget` vs `NodeWidget`
- non-interactive node body vs semantic content region

The fix remains the same in practice: author Value nodes with `render_hint="ref"` unless they are intentionally interactive component nodes.

## Key Code Locations

- `src/editor/visual/node/node_factory.h` - render-hint to widget-class selection
- `src/editor/visual/node/visual_node.cpp` - content geometry and semantic snapshot generation
- `src/editor/input/canvas_input.cpp` - semantic content hit testing and interaction dispatch
- `tests/test_canvas_input.cpp` - semantic interaction regression coverage
