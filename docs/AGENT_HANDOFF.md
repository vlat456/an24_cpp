# Agent Handoff

## Mission

Execute the ongoing global refactor in small, behavior-preserving PRs.

Read first:

- `docs/GLOBAL_REFACTOR_TODO.md`

This file is the short operational handoff. The detailed backlog lives in the file above.

## Core Intent

- Keep `CanvasInput` as the canvas/controller FSM.
- Move node-local interaction and visual-state queries toward `NodeWidget` and content widgets.
- Do not move UI behavior into `bp2::Blueprint::Node` data entities.
- Split large implementation files by functional area.
- Preserve current behavior unless a PR explicitly allows change.

## Architectural Boundary

### Keep In Controller Layer

- selection policy
- multi-select and marquee
- gesture FSM
- viewport/canvas orchestration
- wire creation and reconnection
- undo/redo checkpoint boundaries
- read-only and simulation-mode gating

### Move Toward Visual Node / Content Widgets

- node-local hit testing
- node-local active/energized/pressed visual decisions
- slider/knob/toggle interaction queries
- content-widget-specific drag interpretation

### Do Not Move Into Blueprint Data Model

- `isClicked`
- `isHovered`
- `isActive` as UI state
- `isEnergized` as UI/render state

## Current Known Targets

- `src/editor/document.cpp` (~1434 LOC)
- `src/json_parser/json_parser.cpp` (~1537 LOC)
- `src/core/solvers/jit/build_components.cpp` (~1012 LOC)

`canvas_input.cpp` has already been split previously.

## PR Order

1. Node interaction query API.
2. Node visual state API.
3. Split `build_components.cpp`.
4. Reduce repetition in JIT component registration.
5. Split `document.cpp`.
6. Extract document export helpers.
7. Split `json_parser.cpp`.
8. Normalize parser validation helpers.
9. Cleanup editor input after node API shift.
10. Update knowledge/docs.
11. Run broader validation sweep.

## First PR To Execute

### Goal

Move node-local interaction detection out of `CanvasInput::on_mouse_down()`.

### Desired End State

- `CanvasInput` asks the node/widget layer what interaction was hit.
- `CanvasInput` still decides state transitions and command dispatch.
- Slider/knob/toggle behavior remains unchanged.

### Likely Files

- `src/editor/input/canvas_input.h`
- `src/editor/input/canvas_input_mouse_down.cpp`
- `src/editor/visual/node/visual_node.h`
- `src/editor/visual/node/*.cpp`
- `src/editor/visual/widgets/content_widgets.h`
- `src/editor/visual/widgets/content_widgets.cpp`

### Suggested Shape

- Add a small interaction result type, for example `NodeInteractionHit`.
- Add a `NodeWidget` query method, for example `query_interaction(Pt world_pos)`.
- Move local hit logic for slider/knob/toggle into widget-side code.
- Remove or shrink handler-local helpers:
  - `check_slider_hit()`
  - `check_knob_hit()`
  - `check_content_toggle()`

### Acceptance Criteria

- Less type-specific branching in `on_mouse_down()`.
- No behavior regression.
- Simulation-mode and read-only behavior unchanged.

## Execution Rules

- Prefer the smallest correct refactor.
- Split by concern, not by arbitrary line count only.
- Do not introduce a broad new framework if helper extraction is enough.
- Keep public APIs stable unless the PR explicitly intends otherwise.
- Preserve JSON format and solver behavior.
- If you find an unrelated bug, either isolate the fix clearly or leave it out.

## Validation Rules

For each PR:

- build impacted targets
- run targeted tests for the changed subsystem
- summarize exactly what changed and what was verified

At integration checkpoints:

- run full build
- run full tests

## Deliverable Style

For each PR, produce:

- concise summary
- files changed
- behavioral guarantees
- tests run
- residual risks or follow-ups
