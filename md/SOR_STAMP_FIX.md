# SOR Solver Sign Fix + Unified JSON Format

## Problem
After upgrading to the unified JSON format (compatible with both editor and simulator), JIT and AOT simulators produced NaN values. Investigation revealed the NaN existed **before** the format change — the root cause was in the SOR solver itself.

## Root Cause: `stamp_two_port` Sign Error

The `stamp_two_port` helper in `state.h` computed `through[idx1] += (V1 - V2) * g`, which represents current flowing **OUT** of node idx1. But the SOR update formula `V += omega * through / conductance` expects `through` to be the **residual** — positive when voltage should increase.

The correct residual from a conductance branch is `(V2 - V1) * g` (current flowing **IN**).

### Before (wrong sign → SOR diverges → NaN):
```cpp
float i = (across[idx1] - across[idx2]) * g;
through[idx1] += i;   // WRONG: adds outflow as positive
```

### After (correct residual → SOR converges):
```cpp
float i = (across[idx2] - across[idx1]) * g;
through[idx1] += i;   // CORRECT: inflow is positive residual
```

Components that stamped **directly** (Generator, Gyroscope, AGK47, IndicatorLight, RU19A) already used the correct sign convention. Only `stamp_two_port` and `stamp_one_port_ground` were wrong.

## Secondary Bug: Fixed Signal Handling

The `dynamic_signals_count` mechanism assumed fixed signals are at the end of the array, but ground (signal 0) is always at the beginning. This caused:
- `precompute_inv_conductance()` to compute 1/G for fixed signals instead of 0
- `solve_signals_balance()` to iterate over fixed signals
- AOT `balance_electrical()` to modify ground voltage

**Fix**: All SOR-related functions now use `signal_types[i].is_fixed` check instead of `dynamic_signals_count` boundary.

## JSON Parser Fixes

1. **Port direction case sensitivity**: Added "In", "Out", "InOut" (capitalized) variants to `parse_port_direction()` — required by the unified format
2. **`explicit_domains` field**: Parser now checks `"explicit_domains"` array first, falls back to `"domain"` for backward compatibility
3. **`template_name` field**: Parser now accepts both `"template"` and `"template_name"`

## Files Changed

| File | Change |
|------|--------|
| `src/jit_solver/state.h` | Fix `stamp_two_port` and `stamp_one_port_ground` sign |
| `src/jit_solver/state.cpp` | Fix `precompute_inv_conductance`, `solve_signals_balance`, `solve_signals_balance_fast` to use `is_fixed` |
| `src/jit_solver/components/all.cpp` | Remove Load `-2*i` compensation hack (no longer needed) |
| `src/json_parser/json_parser.cpp` | Add capitalized port directions, `explicit_domains`, `template_name` |
| `src/codegen/codegen.cpp` | Fix generated `balance_electrical` to use `is_fixed` |
| `tests/jit_solver_test.cpp` | Update `PrecomputeInvConductance` test expectation |
| `generated/test_vsu_dmr_aot.cpp` | Fix SOR guard to use `is_fixed` |

## Verification

### JIT and AOT produce identical results:

**Composite test (an24_composite_test.json):**
- Bus voltage: 28.33V (Battery 28V + Generator 28.5V weighted average)
- Ground: 0.00V (fixed)

**VSU DMR test (vsu_dmr_test.json):**
- Bus voltage: 28.10V
- RPM: 60% (idle)
- k_mod: 0.0499
- Brightness: 100

### All tests pass:
- 23/23 jit_solver_tests ✓
- 8/8 json_parser_tests ✓
- 54/54 editor_widget_tests ✓
