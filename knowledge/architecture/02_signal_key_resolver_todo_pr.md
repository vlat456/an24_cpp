# Signal Key Resolver TODO + PR Plan

## Goal

Address two architectural risks before they regress again:

1. Scattered mapping logic (`node.port` vs `node:port.ext` vs external parent prefix mapping)
2. Fragile string-based key plumbing in hot editor paths

## TODO

- [x] Introduce one canonical editor signal-key resolver API for runtime lookup.
- [x] Route all tooltip and energized-wire key construction through that API.
- [x] Eliminate duplicated conditional mapping code from `Document` and `CanvasRenderer`.
- [x] Add a typed endpoint descriptor for resolver inputs (node+port ids + mode context).
- [x] Keep string assembly isolated at the final boundary where simulator API requires string keys.
- [x] Add table-driven tests covering root, expandable root node, and external-ref window modes.
- [x] Add regression test proving root expandable output energization uses mapped runtime key.

## PR Breakdown

### PR 1 — Resolver Core (no behavior change) ✓ DONE

Scope:

- add a small resolver module under editor (single entrypoint)
- add typed request/response structs
- include comprehensive unit tests

Acceptance:

- ~~no callsite migration yet~~
- tests cover all key mapping permutations

### PR 2 — Callsite Migration (behavior preserved) ✓ DONE

Scope:

- migrate `Document::buildEnergizedWireSet`
- migrate `CanvasRenderer` tooltip/wire-hit signal lookup
- migrate external-ref paths to same resolver API

Acceptance:

- all old mapping branches removed from callsites
- behavior identical, code shorter and centralized

### PR 3 — Regression + Safety Hardening ✓ DONE

Scope:

- integration regressions for root expandable and external-ref windows
- add optional debug trace helper (dev-only) for resolved key + value
- eliminated `make_raw_signal_key` duplication (replaced with `editor::build_signal_key`)
- added JIT alias port unification for AOT parity (with regression test)

Acceptance:

- catches future divergence where one UI path forgets resolver

## Constraints

- Keep one-to-one wiring strict.
- Keep FirstOrderLag blueprint composite.
- Preserve 100% AOT-JIT parity.
- No simulation semantics change in this cleanup.

## Code Quality Bar

Implementation must read like neatly bundled cables:

- one canonical path for each concern
- no duplicated branch logic across callsites
- short, explicit helpers with clear names
- minimal conditional nesting and no hidden fallback behavior
- behaviorally identical, structurally cleaner
