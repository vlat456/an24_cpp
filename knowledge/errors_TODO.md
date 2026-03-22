# Architecture Issues & TODO

> Issues discovered during codebase analysis. Prioritize and address as needed.

## High Priority

### 1. Silent Out-of-Bounds in Release Builds
**File:** `src/jit_solver/components/provider.h:47-53`

```cpp
uint32_t get(PortNames p) const {
    auto it = indices.find(p);
    if (it != indices.end()) return it->second;
    assert(false && "unmapped port");  // Only fires in debug!
    return UNMAPPED;  // UINT32_MAX - silently causes OOB array access in release
}
```

**Problem:** In release builds, unmapped ports return `UINT32_MAX`, causing silent out-of-bounds array access.

**Fix Options:**
- Add `[[unlikely]]` branch that throws or logs in release
- Return `std::optional<uint32_t>` and force caller to handle missing case
- Add runtime check: `if (it == indices.end()) { /* handle error */ }`

---

### 2. Dual Blueprint Systems (Incomplete Migration)
**Files:** 
- `src/editor/data/blueprint.h` (legacy)
- `src/blueprint_v2/blueprint/blueprint.h` (new)

**Problem:** Two blueprint implementations coexist. Unclear which is canonical, risks inconsistency.

**Fix:** 
- Complete migration to blueprint_v2
- Add deprecation warnings to legacy
- Document migration status in AGENTS.md

---

## Medium Priority

### 3. PORTS Macro Bloat
**File:** `src/jit_solver/component.h:61-262`

```cpp
#define PORTS_1(Class, p1) uint32_t p1##_idx = 0;
#define PORTS_2(Class, p1, p2) uint32_t p1##_idx = 0; uint32_t p2##_idx = 0;
// ... continues to PORTS_32
```

**Problem:** 200+ lines of repetitive macro definitions for 1-32 ports.

**Fix Options:**
- Use variadic macro with `__VA_ARGS__` 
- Generate with preprocessor script
- Replace with constexpr template metaprogramming

---

### 4. Magic Numbers in Domain Scheduling
**Files:** `src/jit_solver/scheduling.h`, simulation loops

```cpp
if (step % 3 == 0)   // 20 Hz - why 3?
if (step % 12 == 0)  // 5 Hz - why 12?
if (step % 60 == 0)  // 1 Hz - why 60?
```

**Problem:** Division factors are magic numbers scattered across code.

**Fix:**
```cpp
// In scheduling.h or constants.h
namespace Scheduling {
    constexpr int BASE_HZ = 60;
    constexpr int MECHANICAL_DIV = 3;   // 60/20 = 3
    constexpr int HYDRAULIC_DIV = 12;   // 60/5 = 12  
    constexpr int THERMAL_DIV = 60;     // 60/1 = 60
}
```

---

### 5. Deep ComponentVariant Compile Time
**File:** `src/jit_solver/jit_solver.h`

**Problem:** `ComponentVariant` has 80+ alternatives, causing:
- Slow compile times
- Binary bloat
- Long error messages

**Fix Options:**
- Group variants by domain: `ElectricalVariant`, `LogicalVariant`, etc.
- Use type erasure with small buffer optimization
- Consider `std::any` with custom RTTI

---

## Low Priority

### 6. Alignment Without Runtime Verification
**File:** `src/jit_solver/state.h:20-32`

```cpp
alignas(64) std::vector<float> across;
alignas(64) std::vector<float> through;
// ...
```

**Problem:** `alignas` on vector member doesn't guarantee heap allocation alignment. The actual data pointer from `std::vector::data()` depends on allocator.

**Fix:**
- Use custom allocator: `std::vector<float, AlignedAllocator<64>>`
- Or allocate raw buffers: `std::unique_ptr<float[], AlignedDeleter>`

---

### 7. Potential Thread Safety Issues
**Files:** `src/blueprint_v2/` (EditorModel, TypeRegistry)

**Problem:** If editor loads blueprints in background threads while UI accesses them, could cause races.

**Status:** Unknown - needs investigation of actual threading model.

**TODO:** Audit thread usage in editor and add synchronization if needed.

---

## Investigation Needed

### 8. Memory Layout of SimulationState
The SoA design is good for cache locality, but:
- Are vectors resized together?
- Is `dynamic_signals_count` always <= `across.size()`?
- What happens if `allocate_signal` is called after simulation starts?

**TODO:** Add invariant checks in debug builds.

---

### 9. Blueprint V2 Validation Coverage
**File:** `src/blueprint_v2/validation/`

**Questions:**
- Does it catch all cycles in nested blueprints?
- Does it validate domain compatibility on all wire endpoints?
- Are parameter types validated against component expectations?

**TODO:** Review validation coverage against known edge cases.

---

## Summary Table

| # | Issue | Priority | Effort | Impact |
|---|-------|----------|--------|--------|
| 1 | Silent OOB in release | High | Low | Stability |
| 2 | Dual blueprint systems | High | High | Maintainability |
| 3 | PORTS macro bloat | Medium | Medium | Code quality |
| 4 | Magic scheduling numbers | Medium | Low | Readability |
| 5 | ComponentVariant compile time | Medium | High | Build time |
| 6 | Alignment verification | Low | Medium | Performance |
| 7 | Thread safety audit | Low | High | Stability |
| 8 | SimulationState invariants | Low | Low | Debugging |
| 9 | Validation coverage | Low | Medium | Correctness |
