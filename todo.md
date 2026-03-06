# TODO: Push Solver Integration with Editor

## Priority 1: Restore Editor (CRITICAL - без редактора симуляция бессмысленна)

### 1.1 Rewrite app.h/app.cpp for PushSolver
**File:** `src/editor/app.h`, `src/editor/app.cpp`
**Status:** Currently stub, needs full implementation

**Required:**
- [ ] Remove `Simulator<JIT_Solver>` dependency
- [ ] Add `PushSolver` and `PushState` integration
- [ ] Implement voltage display in tooltips
- [ ] Implement wire energization highlighting (yellow for active wires)

**Key types needed:**
```cpp
enum class MouseButton { Left, Right, Middle };
enum class Key { Escape, Delete, ... };
enum class Dragging { CreatingWire, Selecting, Panning, ... };

struct EditorApp {
    Blueprint blueprint;
    Viewport viewport;
    VisualNodeCache visual_cache;
    PushSolver solver;  // ← NEW
    PushState state;    // ← NEW

    void on_mouse_down(Pt world_pos, MouseButton button, Pt screen_pos);
    void on_mouse_up(MouseButton button);
    void on_key_down(Key key);
    // ...
};
```

### 1.2 Fix render.cpp voltage display
**File:** `src/editor/render.cpp`
**Lines:** 130-136, 324-384

**TODOs:**
- [ ] Reimplement wire energization check (line 130-136)
  ```cpp
  // OLD: simulation->wire_is_energized(start_port, 0.5f)
  // NEW: wire_is_energized(state, start_port, 0.5f)
  ```

- [ ] Reimplement tooltip detection (line 324-384)
  ```cpp
  // OLD: simulation->get_port_value(n.id, logical_port)
  // NEW: state.get_voltage(port_ref)
  ```

**Implementation:**
```cpp
// Add helper function in render.cpp
bool wire_is_energized(const PushState& state, const std::string& port_ref, float threshold) {
    float v = state.get_voltage(port_ref);
    return std::abs(v) > threshold;
}
```

### 1.3 Fix an24_editor.cpp compilation errors
**File:** `examples/an24_editor.cpp`
**Errors:** Missing MouseButton, Dragging, Key enums, EditorApp type

**Solution:** Restore from `unused/app.cpp` and adapt for PushSolver

---

## Priority 2: Fix Push Solver Tests

**File:** `tests/test_push_solver.cpp`
**Status:** 6/11 tests passing, 5 failing

### Failing tests:
- [ ] `BatteryDischarge` - expects >27V, gets 25.45V (voltage sag calculation?)
- [ ] `ClosedSwitchPassesVoltage` - expects 0V, gets 28V (switch default state?)
- [ ] `IndicatorLightTurnsOn` - v_in = 1V instead of 28V (connection issue?)
- [ ] `SeriesResistorsCauseVoltageDrop` - no voltage drop (resistance propagation?)
- [ ] `ParallelLoadsDivideCurrent` - no voltage drop (resistance propagation?)

**Root cause:** DeviceInstance.ports not populated in test helper
**Fix:** Update `create_device()` helper to properly initialize ports

---

## Priority 3: PushSolver Improvements

### 3.1 Add missing component methods
**Files:** `src/jit_solver/components/push_components.h`

**Missing:**
- [ ] `override` keyword on `push_voltage()` methods
- [ ] `override` keyword on `propagate_resistance()` methods
- [ ] `update_state()` for Switch/HoldButton with `dt` parameter

### 3.2 Type traits integration
**File:** `src/jit_solver/component_traits.h`
**Status:** Created, not yet used

**TODO:**
- [ ] Use type traits in AOT codegen
- [ ] Consider `if constexpr` for zero-overhead JIT

---

## Priority 4: AOT Codegen Integration

### 4.1 Update codegen for PushSolver
**File:** `src/codegen/codegen.cpp`

**Changes:**
- [ ] Replace `solve_electrical()` calls with `push_voltage(state, dt)`
- [ ] Use ComponentFlags for phase ordering
- [ ] Generate direct calls without vtable lookup

### 4.2 Test AOT generation
```bash
# Should generate optimized code
./codegen_test vsu_test.json
./vsu_aot_test
```

---

## Priority 5: Documentation & Cleanup

- [ ] Update MEMORY.md with ComponentFlags pattern
- [ ] Document PushSolver vs SOR tradeoffs
- [ ] Add Russian comments for all Push components ✅ (DONE)
- [ ] Remove SOR components from `unused/` after AOT verified

---

## Immediate Next Steps (Tomorrow)

1. **Restore editor functionality** (Critical!)
   - Copy `unused/app.cpp` → `src/editor/app.cpp`
   - Replace `Simulator<JIT_Solver>` with `PushSolver`
   - Implement voltage tooltips

2. **Fix render.cpp voltage display**
   - Implement `wire_is_energized()` helper
   - Fix tooltip voltage display

3. **Get editor compiling**
   - Uncomment `examples/CMakeLists.txt` an24_editor target
   - Test manual VSU circuit

---

## Test Results

```
PushSolver Tests: 6/11 passing ✅
- BatteryOutputsNominalVoltage ✅
- BatteryCharge ✅
- WirePropagatesVoltage ✅
- IndicatorLightTurnsOffWhenNoVoltage ✅
- GeneratorNotConnected_NoVoltage ✅
- LerpNodeSmoothsInput ✅

Failing ❌:
- BatteryDischarge (voltage sag too high)
- ClosedSwitchPassesVoltage (default state wrong)
- IndicatorLightTurnsOn (voltage not propagating)
- SeriesResistorsCauseVoltageDrop (resistance not propagating)
- ParallelLoadsDivideCurrent (resistance not propagating)
```

---

## Branch Status

**Branch:** `feature/push-based-solver`
**Last commit:** `69b97b7` - "feat: replace component listing with ComponentFlags bitflags"

**Files changed:** 29 files, 2889 insertions(+), 1135 deletions(-)

**Merged to main:** Not yet, editor needs to be restored first
