# Final Review Findings — Nesting / Signal-Key / Parity

**Date:** 2026-04-01
**Scope:** Full review of all nesting/signal-key/parity work

## Summary

All core signal-key resolution, parser rewrite, and JIT/AOT bridge parity work is
solid and well-tested. Two issues were found and fixed. One low-priority item remains
documented for awareness.

## Findings

| # | Finding | Severity | Status | Fix |
|---|---------|----------|--------|-----|
| F1 | `make_raw_signal_key` in `canvas_renderer.cpp` duplicated `editor::build_signal_key` | Medium | ✓ FIXED | Removed static function, replaced all 4 callsites with `editor::build_signal_key` |
| F2 | JIT missing general alias port unification (AOT codegen had it at 1117-1129) | Medium | ✓ FIXED | Added alias union loop in `jit_solver.cpp` after connection union step. Regression test added: `BlueprintPorts.AliasPortUnification_JitAotParity` |
| F3 | Helper functions (`build_signal_key`, `resolve_external_ref_signal_key`, `map_composite_port_key`) accept empty string inputs producing malformed keys like `".port"` or `"node."` | Low | Documented | Resolver gates empty inputs upstream (returns `""` for empty `node_iid`/`port_iid`). Helper functions are internal, and test 8 (`EmptyComponents`) documents this behavior explicitly. No runtime path can reach helpers with empty inputs. |
| F4 | Parser rewrite contract (`instance.port` → `instance:port.ext`) | — | ✓ Verified OK | Correct and well-guarded with empty-port defensive skip |
| F5 | JIT/AOT bridge ext↔port unification | — | ✓ Verified OK | In parity with PARITY GUARD cross-references |
| F6 | Resolver callsites in Document and CanvasRenderer | — | ✓ Verified OK | Both use canonical `resolve_runtime_signal_key` |
| F7 | Defensive empty endpoint handling in resolver | — | ✓ Verified OK | Returns `""` for empty IDs |

## Files Changed

| File | Change |
|------|--------|
| `src/editor/visual/canvas_renderer.cpp` | Removed `make_raw_signal_key` duplication, added `#include "editor/external_ref_mapping.h"`, replaced 4 callsites with `editor::build_signal_key` |
| `src/jit_solver/jit_solver.cpp` | Added general alias port unification loop (lines 307-325) with PARITY GUARD comment |
| `tests/test_blueprint_integration.cpp` | Added `BlueprintPorts.AliasPortUnification_JitAotParity` regression test |

## Test Results

- **1444 tests total** (1 new test added)
- **1411 ran, 1409 passed, 2 failed** (pre-existing: `SceneMutations.Regression_GSCLoadHasPortsAndWiresVisible`, `V3Migration.GSCIsV3AndDecodes`)
- **0 regressions introduced**
- Regression test verified to fail on old code (alias ports mapped to different signals: 0 vs 1)

## Architectural Health

The nesting/signal-key/parity work is complete and well-guarded:

1. **One canonical resolver** — `resolve_runtime_signal_key()` handles all 3 modes
2. **All callsites migrated** — Document and CanvasRenderer use the resolver
3. **Full JIT/AOT parity** — bridge union, alias union, and parser rewrite are mirrored
4. **16 unit tests + integration tests** — comprehensive coverage including edge cases
5. **PARITY GUARD comments** — cross-reference between JIT and AOT paths
6. **Dev diagnostics** — `AN24_EDITOR_DEBUG_SIGNAL_KEYS=1` env var for future debugging

No remaining architectural concerns specific to this feature area.
