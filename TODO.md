# TODO - Outstanding Tasks

## Date: 2026-03-07

### Test Suite Status
- **Total Tests**: 281 (up from 137 before major re-enable)
- **Passing**: 276/281 (98.2%)
- **Failing**: 5 tests

---

## Priority 1: Fix Failing Tests (5 tests)

### 1. RenderTest.VisualNodeCache_NodeContent_SyncsAfterClear
**File**: `tests/test_render.cpp`
**Issue**: VisualNode cache not syncing after clear operation
**Impact**: Rendering subsystem correctness

### 2. RenderTest.ContentBounds_ExcludesPortLabels
**File**: `tests/test_render.cpp`
**Issue**: Content bounds calculation including port labels when it shouldn't
**Impact**: Visual layout and hit testing

### 3. VisualNodeTest.SetSize_BusIgnoresExternal
**File**: `tests/test_widget.cpp` (likely)
**Issue**: Bus node size logic not respecting external constraints
**Impact**: Visual node sizing

### 4-5. JsonParserTest.OneToOne_MultipleWires* (2 tests)
**File**: `tests/json_parser_test.cpp`
**Issue**: One-to-one validation warns instead of throwing exception
**Current Behavior**: System allows multiple wires from same port with warning
**Expected Behavior**: Should throw exception
**Impact**: Connection validation enforcement
**Note**: These are known tolerance issues - system intentionally allows multiple wires for compatibility

---

## Priority 2: Re-enable Disabled Tests (42 tests)

### jit_solver_tests (37 tests)
**File**: `tests/jit_solver_test.cpp`
**Status**: Disabled - uses old `Systems` API
**Required Changes**:
- Replace `build_result->systems` with `build_result->domain_components`
- Update to use ComponentVariant instead of Systems struct
- Update solve method calls to use std::visit pattern
**Estimated Effort**: 2-3 hours

### factory_validation_tests (5 tests)
**File**: `tests/factory_validation_test.cpp`
**Status**: Disabled - uses old `create_component` API
**Required Changes**:
- Replace `create_component()` with `create_component_variant()`
- Update to new component factory pattern
**Estimated Effort**: 1 hour

---

## Completed Tasks ✅

### 1. Multi-Domain Scheduling (DONE)
- Implemented data-oriented domain-based iteration
- Zero-branch execution for 60/20/5/1 Hz domains
- ComponentVariant integration complete
- Domain bitmask for multi-domain components
**Commit**: `d730dda` - feat(jit): implement data-oriented multi-domain scheduling with bitmask

### 2. Editor Tests Re-enabled (DONE)
- Re-enabled 144 editor tests (render, router, widget)
- Test count: 281 (target was 335, 85% achieved)
- Pass rate: 98.2%
**Commits**:
- `ef71933` - feat(tests): re-enable all editor tests (137 total, 98% passing)
- `f5a777b` - feat(tests): re-enable large editor test suites (281 total, 98% passing)

### 3. RefNode Serialization (DONE)
- Fixed RefNode value parameter serialization
- Added device["params"] with "value": "0.0" for simulator
**Commit**: `ab8b57a` - fix(tests): fix RefNode value serialization in persist

---

## Next Steps

1. **Fix Render/VisualNode tests** (4 tests) - Understand rendering subsystem changes
2. **Update jit_solver_tests** (37 tests) - Port to ComponentVariant API
3. **Update factory_validation_tests** (5 tests) - Port to new factory
4. **AOT Code Generation** - Test blueprint.json → compilation → assembly inspection
5. **Performance Profiling** - Benchmark hot paths after AOT integration

---

## Technical Notes

### ComponentVariant Pattern
All components now use `std::variant<Component1, Component2, ...>` with:
- `static constexpr Domain domain` bitmask field
- Factory function: `create_component_variant()`
- Runtime dispatch: `std::visit` with `if constexpr`

### Multi-Domain Execution
```cpp
// Electrical/Logical: every step (60 Hz)
for (auto* variant : domain_components.electrical) {
    std::visit([&](auto& comp) { comp.solve_electrical(state, dt); }, *variant);
}

// Mechanical: every 3rd step (20 Hz)
if ((step_count % 3) == 0) {
    for (auto* variant : domain_components.mechanical) {
        std::visit([&](auto& comp) { comp.solve_mechanical(state, dt); }, *variant);
    }
}
```

### AOT vs JIT
- **JIT**: Runtime component creation from JSON, uses ComponentVariant
- **AOT**: Generated C++ code with inline component calls (zero indirection)
- **Shared**: Same JSON format, same component interfaces
