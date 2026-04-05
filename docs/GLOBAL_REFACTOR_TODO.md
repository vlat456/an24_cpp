# Global Refactor TODO

## Purpose

This file is the execution backlog for the current global refactor effort. It is intended to be fed to a coding agent PR by PR.

Primary goals:

- Reduce oversized source files to manageable units.
- Reduce per-function complexity and improve local reasoning.
- Preserve current behavior unless a PR explicitly says otherwise.
- Move node-local visual/interaction logic closer to visual node/widgets.
- Keep canvas/document/solver orchestration logic at the controller level.
- Maintain passing tests throughout the sequence.

Current high-level intent:

- `CanvasInput` remains the canvas/controller FSM.
- `NodeWidget` and content widgets own node-local view/interactivity decisions.
- `Document` remains document/application orchestration, but large mixed-responsibility files should be split by concern.
- JIT builder/parser monoliths should be split by functional area, not rewritten into a new architecture in one step.

Constraints:

- Prefer small, behavior-preserving refactors.
- Do not invent broad abstractions unless duplication is concrete and repeated.
- Do not move blueprint data logic into visual widgets.
- Do not move canvas FSM logic into node entities.
- Keep public behavior stable unless acceptance criteria explicitly allow a visible change.
- Run targeted tests on each PR; run broader suites at integration checkpoints.

## Already Done

- `src/editor/input/canvas_input.cpp` split into focused editor input files.
- Duplicate wire-port matching logic extracted from `canvas_input_wires.cpp`.
- Prior failing tests were fixed and full suite reached green at that point.

## Architectural Direction

### Keep In CanvasInput / Controller Layer

- Selection policy.
- Marquee and multi-select.
- Gesture FSM and state transitions.
- Viewport and coordinate-space handling.
- Wire creation/reconnection policy.
- Undo/redo checkpoint boundaries.
- Read-only and simulation-mode gating.

### Move Toward Node/Widget Layer

- Node-local interaction hit testing.
- Node-local visual state derivation.
- Content-widget-specific click/drag interpretation.
- Queries such as energized/active/clickable/pressed visual state.

### Do Not Move Into Blueprint Data Entities

- `isClicked`
- `isHovered`
- `isEnergized`
- any visual interaction state

Those belong in the visual/render/input layer, not in `bp2::Blueprint::Node`.

## PR Backlog

## PR 1: Node Interaction Query API

### Goal

Move node-local interaction detection out of `CanvasInput::on_mouse_down()` and into `NodeWidget` / content widgets.

### Scope

- Introduce a small interaction query API on the visual node side.
- Replace direct handler-side slider/knob/toggle branching with a node query.
- Keep `CanvasInput` in charge of state transitions and command dispatch.

### Candidate Changes

- Add a small result type, for example `NodeInteractionHit`.
- Add a method on `visual::NodeWidget`, for example:
  - `std::optional<NodeInteractionHit> query_interaction(Pt world_pos) const;`
- Let content widgets contribute local hit logic:
  - slider hit and normalized position
  - knob hit / drag start metadata
  - toggle hit
- Remove or shrink:
  - `CanvasInput::check_slider_hit()`
  - `CanvasInput::check_knob_hit()`
  - `CanvasInput::check_content_toggle()`

### Acceptance Criteria

- `CanvasInput::on_mouse_down()` becomes simpler and less type-specific.
- No behavior change for slider, knob, or toggle interaction.
- Simulation-mode behavior remains unchanged.
- Read-only behavior remains unchanged.

### Files Likely Touched

- `src/editor/input/canvas_input.h`
- `src/editor/input/canvas_input_mouse_down.cpp`
- `src/editor/visual/node/visual_node.h`
- `src/editor/visual/node/*.cpp`
- `src/editor/visual/widgets/content_widgets.h`
- `src/editor/visual/widgets/content_widgets.cpp`

### Tests

- Add or update editor interaction tests if present.
- At minimum build editor-related targets and run impacted tests.

## PR 2: Node Visual State API

### Goal

Move node-local visual-state derivation such as active/energized/clickable/pressed-state decisions closer to visual widgets.

### Scope

- Define what belongs to render context vs node widget vs content widget.
- Avoid putting UI state into blueprint data entities.
- Make render code ask widgets for local state where appropriate.

### Candidate Changes

- Add helper/query methods on `NodeWidget` or content widgets for:
  - active visual state
  - energized visual state if derivable from current render data
  - pressed/clicked visual state
- Keep frame-global data in `RenderContext`.
- If useful, add a small immutable `NodeVisualState` struct computed at render time.

### Acceptance Criteria

- Fewer ad hoc visual-state checks spread across renderer/input code.
- No persistent visual interaction state leaks into blueprint model.
- Render path remains behavior-compatible.

### Files Likely Touched

- `src/editor/visual/render_context.h`
- `src/editor/visual/node/visual_node.h`
- `src/editor/visual/node/*.cpp`
- `src/editor/visual/widgets/content_widgets.*`
- possibly focused renderer files

### Notes

- This PR should stay local. Do not redesign the full render system.

## PR 3: Split `build_components.cpp`

### Goal

Refactor `src/core/solvers/jit/build_components.cpp` into smaller functional units without changing build behavior.

### Scope

- Keep `build_and_register_components()` as the public entry point.
- Extract component registration by category.
- Extract shared helper utilities for repetitive registration patterns.

### Suggested Split

- `build_components.cpp`
  - top-level loop and orchestration only
- `build_components_common.h/.cpp`
  - shared helpers like port setup and common register helpers
- `build_components_electrical.cpp`
- `build_components_logic.cpp`
- `build_components_control.cpp`
- `build_components_mechanical.cpp`
- `build_components_validation.cpp`
  - `validate_source_writer_conflicts`
  - `validate_consumer_guardrails`
  - `topological_sort_consumers`

### Acceptance Criteria

- No behavioral changes in component construction or scheduler registration.
- Original monolith reduced substantially.
- No new dynamic registration framework introduced.
- JIT solver tests pass.

### Files Likely Touched

- `src/core/solvers/jit/build_components.cpp`
- new split files under `src/core/solvers/jit/`
- `src/core/solvers/jit/jit_solver_internal.h`
- `src/core/solvers/jit/CMakeLists.txt`

### Tests

- Build `jit_solver` and dependent tests.
- Run targeted solver/parser/editor tests that exercise build paths.

## PR 4: Reduce Repetition In JIT Component Registration

### Goal

Remove obvious repetitive registration patterns inside the new split JIT builder files.

### Scope

- Only extract helpers where repetition is concrete.
- Prefer a few small templates/helpers over a registry rewrite.

### Candidate Changes

- Shared helper for:
  - `setup_ports(comp)`
  - `param_reader.validate_all_consumed()`
  - storing `result.devices[dev.name] = comp`
  - optional scheduler source/consumer registration
- Keep special cases inline where they are materially different.

### Acceptance Criteria

- Net reduction in repeated boilerplate.
- New helper layer stays readable.
- Factory code remains grep-friendly when adding a component.

### Notes

- Do not switch to self-registering factories in this PR.

## PR 5: Split `document.cpp` By Responsibility

### Goal

Refactor `src/editor/document.cpp` into functional compilation units.

### Scope

- Preserve `Document` as the orchestration facade.
- Split implementation by concern, not by arbitrary size.

### Suggested Split

- `document.cpp`
  - constructor, title, high-level glue only
- `document_io.cpp`
  - save/load, serialization helpers
- `document_simulation.cpp`
  - start/stop/rebuild/update simulation
- `document_input.cpp`
  - `applyInputResult`, gesture-related document responses
- `document_windows.cpp`
  - sub-window open/rebuild logic
- `document_export.cpp`
  - simulation JSON export and related helpers

### Acceptance Criteria

- `document.cpp` reduced substantially.
- No visible regression in save/load/simulation/window behavior.
- Cross-file private helper usage remains understandable.

### Files Likely Touched

- `src/editor/document.cpp`
- new `src/editor/document_*.cpp`
- `examples/CMakeLists.txt` and/or editor target definitions if needed
- test CMake files if they enumerate sources directly

### Tests

- Editor/document tests.
- Save/load related tests.
- Simulation-start/build smoke coverage.

## PR 6: Extract Document Export Helpers

### Goal

Further simplify document-side simulation export logic after the split.

### Scope

- Extract local helper functions/structs from `build_simulation_json()`.
- Keep the external `Document` API unchanged.

### Candidate Changes

- helper for device emission
- helper for port serialization
- helper for visual-only/int param filtering
- helper for proxy-node skipping / connection rewrite logic

### Acceptance Criteria

- `build_simulation_json()` is shorter and easier to read.
- No JSON format change unless explicitly intended and validated.

## PR 7: Split `json_parser.cpp` By Parse Domain

### Goal

Refactor `src/json_parser/json_parser.cpp` into smaller parser units.

### Scope

- Keep public parser API stable.
- Split helpers by topic.
- Preserve error message quality.

### Suggested Split

- `json_parser.cpp`
  - top-level API only
- `json_parser_types.cpp`
  - enum/string/domain/port conversions
- `json_parser_schema.cpp`
  - param schema parsing and validation
- `json_parser_devices.cpp`
  - device parsing
- `json_parser_connections.cpp`
  - connection parsing and compatibility logic
- `json_parser_blueprints.cpp`
  - blueprint/composite expansion helpers if currently mixed in

### Acceptance Criteria

- Main file size reduced heavily.
- No parser behavior regressions.
- Unit/integration tests around JSON parsing stay green.

### Tests

- parser tests
- blueprint import/load tests
- solver build smoke tests from parsed JSON

## PR 8: Normalize Parser Validation Helpers

### Goal

Reduce repeated validation/error-building patterns in the parser after file split.

### Scope

- Focus on repeated field checks and typed extraction helpers.
- Keep thrown error text stable where practical.

### Acceptance Criteria

- Less repeated object/type checking code.
- Readability improves without hiding parser behavior.

## PR 9: Editor Input Cleanup After Node API Shift

### Goal

After PR 1 and PR 2, simplify remaining editor input code based on the new widget-local interaction APIs.

### Scope

- Remove dead helpers.
- Shorten large event handlers.
- Keep behavior fixed.

### Candidate Changes

- shrink `on_mouse_down()` branching
- centralize repeated simulation-mode and normal-mode node interaction paths
- make interaction dispatch table/flow more explicit if it can be done minimally

### Acceptance Criteria

- Less duplicated node hit handling.
- No new abstraction layers unless they clearly pay off.

## PR 10: Knowledge Base Update

### Goal

Update project docs to reflect the new file layout and the intended architecture boundary.

### Scope

- Document canvas-controller vs node-widget responsibilities.
- Update JIT knowledge/docs for split factory files.
- Update quick reference paths if source files move.

### Files Likely Touched

- `knowledge/05_editor.md`
- `knowledge/knowledge_jit.md`
- `knowledge/10_quick_reference.md`
- optionally `knowledge/errors_TODO.md` if an item is materially addressed

### Acceptance Criteria

- New developers can find the correct file quickly.
- Architectural intent is documented, not just implemented.

## PR 11: Full Validation Sweep

### Goal

Run the broader validation/build/test sweep after the structural PRs land.

### Scope

- clean configure if needed
- full build
- full tests
- investigate regressions caused by file split/linkage issues

### Acceptance Criteria

- Repository builds cleanly.
- Full test suite passes.
- Any failures are either fixed or written down with exact scope/blocker.

## Suggested Order

1. PR 1: Node Interaction Query API
2. PR 2: Node Visual State API
3. PR 3: Split `build_components.cpp`
4. PR 4: Reduce Repetition In JIT Component Registration
5. PR 5: Split `document.cpp`
6. PR 6: Extract Document Export Helpers
7. PR 7: Split `json_parser.cpp`
8. PR 8: Normalize Parser Validation Helpers
9. PR 9: Editor Input Cleanup After Node API Shift
10. PR 10: Knowledge Base Update
11. PR 11: Full Validation Sweep

## Guardrails For Coding Agent

- Before each PR, inspect current file ownership and recent changes.
- Do not mix architectural cleanup with behavioral feature work.
- Prefer extracting existing logic over inventing new patterns.
- If a helper is only used once or twice, leave it inline.
- If tests expose an existing bug during refactor, fix it in a dedicated commit or clearly separate it in the PR description.
- Avoid moving logic into `bp2::Blueprint::Node` unless it is truly persistent model data.
- Prefer `NodeWidget` / content widget methods over handler-side type checks for node-local interaction.
- Preserve user-facing JSON format and command behavior unless explicitly changing them.

## Definition Of Done For The Global Effort

- Large target files are split into coherent units.
- Canvas input code owns orchestration, not node-local widget behavior.
- Node/content widgets own local interaction and visual-state queries.
- Parser and JIT builder monoliths are significantly reduced.
- Tests are green.
- Knowledge base reflects the new layout and architectural boundaries.
