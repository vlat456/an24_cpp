# An-24 Codebase Improvements Analysis

**Date:** 2026-03-14  
**Status:** 1220/1220 tests passing

---

## Completed Today (2026-03-14)

- [x] Full InternedId migration for all 5 visual widgets
- [x] CMake integration for `port_registry.h` regeneration (`cmake --build build --target regenerate_port_registry`)
- [x] Compile-time sync guard: `static_assert(variant_size == ComponentType::_COUNT)`
- [x] **SimulationController → Simulator unification**
  - Deleted `src/editor/simulation.h` and `src/editor/simulation.cpp` (~200 lines of duplication)
  - Updated all tests to use `Simulator<JIT_Solver>`
  - Tests now benefit from multi-domain physics (mechanical/hydraulic/thermal)
- [x] **Multi-Domain Scheduling Test Coverage**
  - Created `tests/test_multi_domain.cpp` with 8 tests
  - Verifies mechanical (20 Hz), thermal (1 Hz), electrical (60 Hz) scheduling
  - Tests domain sorting and multi-domain components

---

## Remaining Improvements

### 1. Replace Component Factory if-else Chain with Registry Pattern ⭐ HIGH IMPACT

**Problem:** `create_component_variant()` in `jit_solver.cpp` uses 450+ line if-else chain for 66 component types. Adding a new component requires editing multiple files.

**Location:** `src/jit_solver/jit_solver.cpp:70-516`

**Impact:** HIGH - Developer friction, O(N) lookup

**Complexity:** Medium

**Suggested pattern:**
```cpp
#define REGISTER_COMPONENT(Classname) \
    factory.emplace(#Classname, [](auto& dev, auto& result) { \
        return make_variant<Classname>(dev, result); \
    })
```

---

### 2. Fix Silent Exception Swallowing

**Problem:** 2 instances of `catch (...) {}` still silently discard errors in `codegen.cpp`

**Location:** `src/codegen/codegen.cpp:41, 49` (type inference functions)

**Impact:** MEDIUM - Debugging difficulty

**Complexity:** Small - Add `spdlog::debug()` calls

---

### 3. Resolve Incomplete TODOs

| Location | Issue | Complexity |
|----------|-------|------------|
| `json_parser.cpp:398` | `|| true` hack bypasses validation | Small |
| `all.h:561` | Spring damping coefficient unused | Small |
| `canvas_input.cpp:589` | Wire auto-routing not connected | Medium |

**Impact:** MEDIUM

---

### 4. Build System: Benchmark Infrastructure

**Problem:** `benchmarks/CMakeLists.txt` exists but `benchmarks/*.cpp` is empty.

**Impact:** LOW

**Complexity:** Medium

---

### 5. Enhance Error Messages

**Problem:** Terse error messages without context:
- `"Unknown component type: " + classname` - doesn't list valid types
- Port lookup failures don't suggest similar names

**Impact:** LOW

**Complexity:** Small

---

## Summary

| Task | Impact | Complexity | Status |
|------|--------|------------|--------|
| Component Factory Registry | HIGH | Medium | 🔴 Pending |
| Fix Silent Exceptions (2 left) | MEDIUM | Small | 🔴 Pending |
| Resolve TODOs | MEDIUM | Small-Medium | 🔴 Pending |
| Benchmark Infrastructure | LOW | Medium | 🔴 Pending |
| Enhance Error Messages | LOW | Small | 🔴 Pending |

**Recommended next step:** Component Factory Registry (highest impact)
