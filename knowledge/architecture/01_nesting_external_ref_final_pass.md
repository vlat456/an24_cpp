# Nesting / External Reference Final Pass

## Context

This note captures architecture observations and improvement suggestions after stabilizing:

- `FirstOrderLag` extraction as library composite blueprint
- root-level signal mapping for expandable composite nodes
- external-reference window rendering with parent-bound signal keys
- bridge semantics parity between JIT and AOT (`BlueprintInput/BlueprintOutput` ext↔port)

## What Is Good Now

- Composite expansion semantics are explicit and tested.
- Root-level visualization correctly maps expandable node ports (`node.port` → `node:port.ext`).
- External-reference windows use parent-bound signal mapping, not disconnected document context.
- AOT parity improved: codegen now unifies bridge ext↔port like JIT.
- Regressions exist for mapping and bridge behavior.

## Current Architectural Smells

1. Signal-key mapping logic remains spread across editor paths (document render, tooltip/wire hit, external windows).
2. Mapping is string-based and repeated in hot UI code paths.
3. Root vs external-ref key resolution is behaviorally similar but represented by separate conditional flows.
4. External reference windows are read-only, but data-flow boundaries are implicit rather than modeled as first-class contracts.
5. AOT/JIT parity is covered by targeted tests but not centralized as a formal compatibility checklist in code comments near key expansion/unification logic.

## Recommended Target Shape

Introduce a single "signal key resolver" service for editor runtime lookup.

Suggested responsibilities:

- Input: window mode (root / embedded / external-ref), hit endpoint identity (node_iid/port_iid), optional parent instance id.
- Output: canonical simulator key used by `get_wire_voltage` / `wire_is_energized`.

This removes scattered string assembly and reduces divergence risk.

## Staged Improvement Plan

### Stage 1 (small, safe) ✓ DONE

- Consolidated all editor signal-key mapping calls behind one helper API (`signal_key_resolver.h/.cpp`).
- Behavior preserved exactly.
- Table-driven unit tests covering all mapping modes (16 tests in `test_signal_key_resolver.cpp`).

### Stage 2 (medium) — DEFERRED

- Replace string-heavy path in render/tooltip with interned intermediate form where possible.
- Convert only at simulator call boundary.
- **Status:** Deferred. Current string-based resolver is fast enough for UI paths.

### Stage 3 (medium) ✓ DONE

- Added explicit PARITY GUARD comments near:
  - JIT union-find bridge unification (`jit_solver.cpp:271-289`)
  - JIT alias port unification (`jit_solver.cpp:307-325`)
  - AOT codegen bridge unification (`codegen.cpp:1089-1105`)
  - AOT codegen alias unification (`codegen.cpp:1117-1129`)
  - Parser parent-port rewrite logic (`json_parser.cpp:570-624`)
- JIT alias port union step added to match AOT (was previously missing).

### Stage 4 (optional) ✓ DONE

- Added dev-only diagnostics in `canvas_renderer.cpp`:
  - Env var `AN24_EDITOR_DEBUG_SIGNAL_KEYS=1` enables trace logging
  - Shows: visual node, visual port, resolved key, current value
  - Active for both port hover and wire hover paths

## Guardrails

- Keep one-to-one wiring rule strict.
- Do not reintroduce root-level fallback lookups that bypass rewritten runtime keys.
- Maintain 100% AOT-JIT parity for bridge semantics and nested expansion.
- Keep external reference windows read-only unless a full writeback model is designed.

## Suggested Regression Set (must stay green)

- Root expandable port tooltip/energized mapping regressions
- External reference signal mapping integration regressions
- AOT composite bridge ext↔port unification regression
- Closed-circuit RN180 regulated behavior regressions
