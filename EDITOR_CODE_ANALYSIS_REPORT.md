# Editor Code Analysis Report

**Date:** 2026-03-08  
**Scope:** `/src/editor/` directory (C++ editor component)  
**Analysis Type:** Static code review - bugs, code smells, architectural issues

---

## Executive Summary

The editor codebase demonstrates solid engineering with good separation of concerns, but has several categories of issues ranging from critical bugs to architectural concerns. The code shows evidence of active development with many "BUGFIX" comments indicating iterative improvements.

**Key Findings:**
- **Critical Bugs:** 3 issues (group filtering, state management)
- **High Priority:** 5 issues (memory safety, initialization, deduplication)
- **Medium Priority:** 8 issues (code smells, maintainability)
- **Low Priority:** 6 issues (style, potential improvements)

---

## 1. CRITICAL BUGS

### 1.1 Group Filtering Inconsistencies

**Location:** Multiple files  
**Severity:** CRITICAL  
**Impact:** Cross-contamination between blueprint hierarchy levels

**Issue:** Several functions were missing `group_id` filtering, causing nodes/wires from different hierarchy levels to be processed together:

- `CanvasInput::finish_marquee()` - was selecting nodes from ALL groups instead of current group only (comment `[BUG-c3d4]`)
- `hit_test_routing_point()` - was matching routing points from other blueprint groups (comment `[BUGFIX [3f7b9c]]`)
- `hit_test()` wire segment checking - had group filtering added but consistency across all hit-test functions is fragile

**Risk:** Users can accidentally manipulate nodes in nested blueprints from parent view, breaking hierarchy integrity.

**Recommendation:** Implement compile-time enforcement of group filtering via type system (e.g., `FilteredView<Blueprint>` that only exposes group-filtered iterators).

---

### 1.2 Simulation State Not Reset on Stop

**Location:** `src/editor/app.cpp:reset_node_content()`  
**Severity:** CRITICAL  
**Impact:** Visual state inconsistency after simulation stop

**Issue:** When simulation stops, `reset_node_content()` resets visual node content, but there's no guarantee that all component types are handled. The function has explicit handling for:
- Voltmeter, IndicatorLight, DMR400, Switch, HoldButton

Any new component type added to the simulation will NOT have its visual state reset unless this function is updated.

**Risk:** Components may show incorrect state after simulation stop (e.g., lights staying ON, switches showing wrong position).

**Recommendation:** 
1. Add static assertion or registration mechanism to ensure all components provide default visual state
2. Consider storing initial state in component definition and auto-resetting

---

### 1.3 Uninitialized Struct Members

**Location:** `src/editor/visual/hittest.h:HitResult`  
**Severity:** HIGH  
**Impact:** Undefined behavior in hit testing

**Issue:** Comment `[BUGFIX [4k9m2x7p]]` indicates `port_side` was uninitialized, causing UB when `type != Port`. While marked as fixed, the struct still relies on value initialization:

```cpp
struct HitResult {
    HitType type = HitType::None;  // Some fields initialized, others not
    size_t node_index;              // Uninitialized
    size_t wire_index;              // Uninitialized
    // ...
};
```

**Risk:** If any code path creates `HitResult` without full initialization, reads of uninitialized members is UB.

**Recommendation:** Use aggregate initialization with explicit defaults for ALL members, or convert to class with constructor.

---

## 2. HIGH PRIORITY ISSUES

### 2.1 Duplicate Deduplication Relies on Runtime Checks

**Location:** `src/editor/data/blueprint.cpp`, `src/editor/visual/scene/persist.cpp`  
**Severity:** HIGH  
**Impact:** Potential data corruption from duplicate entities

**Issue:** Multiple "BUGFIX [e4a1b7]" comments show duplicate nodes/wires are rejected at runtime with warnings:

```cpp
// BUGFIX [e4a1b7] Runtime dedup: reject exact-duplicate wires with warning
size_t Blueprint::add_wire(Wire wire) {
    for (const auto& w : wires) {
        if (w.start.node_id == wire.start.node_id && ...) {
            spdlog::warn("[dedup] Runtime duplicate wire rejected...");
            return SIZE_MAX;
        }
    }
    ...
}
```

**Risk:** 
- Performance degradation on large blueprints (O(n) check per add)
- Silent failures - duplicates are logged but caller may not check return value
- Indicates deeper architectural issue: ID generation is not globally unique

**Recommendation:** 
1. Use `std::set` or `std::unordered_set` with custom hashers for O(1) dedup
2. Implement proper unique ID generation (UUID or monotonic counter with set tracking)

---

### 2.2 Raw Pointer References in Non-Owning Relationships

**Location:** `src/editor/visual/scene/scene.h`, `src/editor/window/window_manager.h`  
**Severity:** HIGH  
**Impact:** Potential dangling pointers

**Issue:** 
```cpp
class VisualScene {
private:
    Blueprint* bp_;  // Raw non-owning pointer
    ...
};

class WindowManager {
private:
    Blueprint& bp_;  // Reference
    std::vector<std::unique_ptr<BlueprintWindow>> windows_;
};
```

**Risk:** If `Blueprint` is destroyed while `VisualScene` or `BlueprintWindow` still references it, dangling pointer occurs. The comment "Non-owning reference" in `BlueprintWindow` acknowledges this but doesn't prevent misuse.

**Recommendation:** Use `std::weak_ptr` pattern or `gsl::not_null<Blueprint*>` to make lifetime dependencies explicit and checked.

---

### 2.3 Magic Numbers in Layout Code

**Location:** `src/editor/visual/node/node.cpp`, `src/editor/data/blueprint.cpp`  
**Severity:** MEDIUM-HIGH  
**Impact:** Maintainability, visual inconsistency

**Issue:** Hardcoded values throughout layout code:
```cpp
constexpr float PORT_LAYOUT_GRID = 16.0f;  // Different from GRID_STEP
constexpr float LAYOUT_GRID = 16.0f;
constexpr float col_spacing = 200.0f;
constexpr float row_spacing = 120.0f;
```

Comment `[BUGFIX [8d4e6a]]` specifically mentions mismatch between `PORT_LAYOUT_GRID` (16.0f) and user-facing grid settings (4/8/12px).

**Risk:** Visual misalignment, confusing UX when different grid systems conflict.

**Recommendation:** Centralize grid constants in single configuration header with clear documentation of which grid is used for which purpose.

---

### 2.4 Exception Safety in JSON Parsing

**Location:** `src/editor/visual/scene/persist.cpp`  
**Severity:** HIGH  
**Impact:** Crash on malformed JSON

**Issue:** 
```cpp
static PortType parse_port_type_str(const std::string& s) {
    if (s == "V") return PortType::V;
    // ...
    spdlog::error("Unknown port type string: '{}'", s);
    std::abort();  // Hard crash on unknown type
}
```

**Risk:** Single malformed blueprint file crashes entire application.

**Recommendation:** Return `std::optional<PortType>` and propagate error up to user-visible error message with file/line info.

---

### 2.5 Bus Port Management Complexity

**Location:** `src/editor/visual/node/node.cpp:BusVisualNode`  
**Severity:** HIGH  
**Impact:** Maintenance burden, potential synchronization bugs

**Issue:** `BusVisualNode` has complex port management with multiple data structures that must stay in sync:
- `ports_` vector (visual ports)
- `wires_` vector (connected wires)
- Alias ports named by wire ID

Methods like `connectWire()`, `disconnectWire()`, `recalculatePorts()` all manipulate these structures with subtle dependencies.

**Risk:** Port/wire desynchronization leads to visual glitches or connection failures.

**Recommendation:** 
1. Encapsulate port management in dedicated `BusPortManager` class
2. Add invariants/assertions to verify consistency
3. Consider event-driven updates instead of manual recalculation

---

## 3. MEDIUM PRIORITY (CODE SMELLS)

### 3.1 God Class: `EditorApp`

**Location:** `src/editor/app.h`, `src/editor/app.cpp`  
**Severity:** MEDIUM  
**Impact:** Maintainability, testability

**Issue:** `EditorApp` has too many responsibilities:
- Scene management
- Window management  
- Simulation lifecycle
- Component registry
- Blueprint scanning
- Signal overrides
- Context menu state
- Button state tracking

**Methods:** 20+ public methods, mixing UI, simulation, and data management.

**Recommendation:** Split into:
- `EditorApplication` (lifecycle, coordination)
- `SimulationController` (already exists but underutilized)
- `ComponentCatalog` (registry + blueprint scanning)
- `UIStateManager` (context menus, button states)

---

### 3.2 Inconsistent Naming Conventions

**Location:** Throughout codebase  
**Severity:** MEDIUM  
**Impact:** Readability, onboarding

**Issue:** Mixed naming styles:
- `snake_case` for functions: `add_component`, `rebuild_simulation`
- `camelCase` for some methods: `open_sub_window` (actually snake), but `node_content` field
- Hungarian-ish notation: `bp_`, `vp_`, `dl_` for member variables
- Russian comments mixed with English code

**Example from single file:**
```cpp
void add_component(...)      // snake_case
void rebuild_simulation()    // snake_case  
node_content                 // snake_case field
bp_                          // member suffix
```

**Recommendation:** Adopt consistent style guide (recommend Google C++ style: `snake_case` for functions/variables, `CamelCase` for types).

---

### 3.3 Deep Inheritance for Visual Nodes

**Location:** `src/editor/visual/node/node.h`  
**Severity:** MEDIUM  
**Impact:** Rigidity, fragile base class problem

**Issue:** Inheritance hierarchy:
```
VisualNode (base)
├── BusVisualNode
└── RefVisualNode
```

`VisualNode` has 10+ virtual methods, many with empty default implementations. `BusVisualNode` overrides 5+ methods with Bus-specific logic.

**Risk:** Adding new node types requires understanding entire hierarchy. Multiple inheritance concerns (IDrawable, ISelectable, IDraggable, IPersistable) complicate matters.

**Recommendation:** Consider composition over inheritance:
- `NodeRenderer` component with strategy pattern for different rendering modes
- Remove empty virtual methods, use optional interfaces

---

### 3.4 Macro-Based Port Generation

**Location:** `src/jit_solver/component.h`  
**Severity:** MEDIUM  
**Impact:** Debuggability, IDE support

**Issue:** 200+ lines of PORTS macros (PORTS_1 through PORTS_32):
```cpp
#define PORTS_5(Class, p1, p2, p3, p4, p5) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; ...
```

**Problems:**
- Macros are invisible to IDE refactoring tools
- Error messages point to macro expansion, not usage
- No type safety - port names are stringified

**Recommendation:** Use C++20 templates or constexpr functions instead of preprocessor macros.

---

### 3.5 Missing Const Correctness

**Location:** Multiple files  
**Severity:** MEDIUM  
**Impact:** API clarity, optimization opportunities

**Issue:** Methods that should be const are not:
```cpp
// src/editor/visual/scene/scene.h
std::vector<Node>& nodes() { return bp_->nodes; }  // OK
const std::vector<Node>& nodes() const { return bp_->nodes; }  // OK

// But VisualNode methods often missing const:
const VisualPort* getPort(const std::string& name) const;  // Has const
void recalculatePorts();  // Missing const - modifies internal state?
```

**Recommendation:** Audit all methods for const correctness, mark mutation-intending methods explicitly.

---

### 3.6 Error Handling via Return Values

**Location:** Throughout codebase  
**Severity:** MEDIUM  
**Impact:** Silent failures

**Issue:** Many functions return bool/optional but callers don't check:
```cpp
if (scene_.addWire(std::move(w)))  // At least this one checks
    result.rebuild_simulation = true;

bool Blueprint::add_wire_validated(Wire wire) {
    if (!are_ports_compatible(...)) return false;  // Caller may ignore
    ...
}
```

**Recommendation:** 
1. Use `[[nodiscard]]` attribute on critical return values
2. Consider exception-throwing variants for unrecoverable errors
3. Add static analysis to detect unchecked returns

---

### 3.7 Tight Coupling to ImGui

**Location:** `examples/an24_editor.cpp:ImGuiDrawList`  
**Severity:** MEDIUM  
**Impact:** Testability, backend flexibility

**Issue:** `IDrawList` interface is designed for ImGui but claims to be abstract:
```cpp
class ImGuiDrawList : public IDrawList {
public:
    ImDrawList* dl = nullptr;  // ImGui-specific
    // All methods directly call ImGui APIs
};
```

**Risk:** Cannot test rendering logic without ImGui context. Cannot swap rendering backend.

**Recommendation:** 
1. Create true abstraction layer (e.g., `Canvas` interface)
2. Add mock implementation for unit tests
3. Move ImGui-specific code to backend module

---

## 4. LOW PRIORITY (STYLE & MAINTAINABILITY)

### 4.1 Inconsistent Comment Languages

**Issue:** Mix of English and Russian comments:
```cpp
/// Узел в схеме - компонент (батарея, насос, и т.д.)  // Russian
/// Blueprint - схема соединений (все домены)          // Russian
/// JIT Simulation (manages component lifecycle)       // English
```

**Recommendation:** Standardize on English for international collaboration.

---

### 4.2 Dead Code Markers Without Cleanup

**Location:** Multiple files  
**Issue:** Many `[DEAD-xxxx]` comments marking removed code that's still present:
```cpp
// [DEAD-g7h8] Removed dead code: was setting blueprint_path...
// But the code block is still there
```

**Recommendation:** Actually remove dead code instead of commenting.

---

### 4.3 Large Header Files

**Location:** `src/jit_solver/component.h` (300+ lines, mostly macros)  
**Issue:** Header files contain implementation details, increasing compile times.

**Recommendation:** Move macro definitions to separate `.inl` or implementation files.

---

### 4.4 Missing Documentation for Public APIs

**Location:** Most header files  
**Issue:** While some structs have comments, many public methods lack documentation:
- Parameter semantics unclear
- Return value contracts not specified
- Thread safety not documented

**Recommendation:** Add Doxygen-style comments for all public APIs.

---

### 4.5 Test Coverage Gaps

**Location:** `tests/` directory  
**Issue:** Based on file names, tests exist for:
- `test_editor_hierarchical.cpp`
- `test_visual_scene.cpp`
- `test_hittest.cpp`

But no visible tests for:
- `EditorApp` integration
- `WindowManager` multi-window scenarios
- Persistence round-trip validation
- Simulation reset behavior

**Recommendation:** Add integration tests for critical paths.

---

### 4.6 Performance Anti-patterns

**Locations:** Multiple

**Issues:**
1. **O(n²) wire rendering:** `WireRenderer` builds node_map (good) but crossing detection is still O(w²) for w wires
2. **Cache invalidation:** `VisualNodeCache::clear()` nukes entire cache on any change
3. **String copying:** Port names copied frequently in hot paths

**Recommendation:** Profile and optimize hot paths, use string_view where appropriate.

---

## 5. ARCHITECTURAL CONCERNS

### 5.1 Flattened Hierarchy Strategy

**Location:** `src/editor/app.cpp:add_blueprint()`  
**Issue:** Nested blueprints are immediately flattened with prefixed IDs:
```cpp
// IMMEDIATELY EXPAND the nested blueprint (always-flatten architecture)
node.id = prefix + ":" + dev.name;
```

**Pros:** Simple simulation, no hierarchical component resolution needed  
**Cons:** 
- Loses hierarchical structure in simulation
- Wire rewriting is fragile
- Cannot edit nested blueprint in-place

**Assessment:** Acceptable for current scope but limits future features (hierarchical simulation, encapsulated sub-circuits).

---

### 5.2 Variant-Based Component Storage

**Location:** `src/jit_solver/jit_solver.h:BuildResult`  
**Issue:** Components stored as `std::unordered_map<std::string, ComponentVariant>` where `ComponentVariant` is `std::variant<...>` of all component types.

**Pros:** Type-safe, no raw pointers  
**Cons:** 
- O(n) variant visitation per component per step
- Adding component type requires updating variant type list
- Memory fragmentation from variant size

**Assessment:** Reasonable for editor (JIT) but would not scale to large simulations.

---

### 5.3 Single Shared Blueprint Across Windows

**Location:** `src/editor/window/window_manager.h`  
**Issue:** All `BlueprintWindow` instances share reference to single `Blueprint`:
```cpp
class WindowManager {
    Blueprint& bp_;  // Shared by all windows
    std::vector<std::unique_ptr<BlueprintWindow>> windows_;
};
```

**Risk:** 
- No isolation between windows
- Cannot have two views of different blueprints simultaneously
- Thread safety if multi-threaded rendering added

**Assessment:** Works for current single-blueprint editor, blocks future multi-document interface.

---

## 6. POSITIVE OBSERVATIONS

Despite the issues above, the codebase demonstrates several strong engineering practices:

1. **Explicit bug tracking:** BUGFIX comments with commit hashes enable追溯
2. **Performance annotations:** `[PERF-xxxx]` comments show optimization awareness
3. **Dead code marking:** `[DEAD-xxxx]` shows intent to clean up
4. **Separation of concerns:** Visual/data separation generally well-maintained
5. **Type safety efforts:** PortType enums, ComponentVariant instead of void*
6. **Logging infrastructure:** spdlog integration for diagnostics

---

## 7. RECOMMENDED PRIORITY ORDER

### Immediate (Next Sprint):
1. Fix group filtering consistency (1.1)
2. Add `[[nodiscard]]` to critical return values (3.6)
3. Initialize all HitResult members (1.3)

### Short-term (1-2 months):
4. Refactor duplicate deduplication (2.1)
5. Add simulation state reset tests (1.2)
6. Centralize grid constants (2.3)

### Medium-term (Quarter):
7. Split EditorApp god class (3.1)
8. Improve error handling in JSON parsing (2.4)
9. Add integration tests for persistence

### Long-term:
10. Consider composition over inheritance for nodes (3.3)
11. Evaluate macro replacement strategy (3.4)
12. Profile and optimize rendering hot paths (4.6)

---

## APPENDIX A: Files Analyzed

```
src/editor/
├── app.h, app.cpp
├── simulation.h, simulation.cpp
├── data/
│   ├── blueprint.h, blueprint.cpp
│   ├── node.h
│   ├── wire.h
│   ├── port.h
│   └── pt.h
├── visual/
│   ├── node/
│   │   ├── node.h, node.cpp
│   │   ├── widget.h
│   │   └── port.h
│   ├── scene/
│   │   ├── scene.h
│   │   ├── wire_manager.h
│   │   └── persist.h, persist.cpp
│   ├── renderer/
│   │   ├── blueprint_renderer.h
│   │   ├── node_renderer.h
│   │   ├── wire_renderer.h, wire_renderer.cpp
│   │   ├── grid_renderer.h
│   │   ├── tooltip_detector.h
│   │   └── render_theme.h
│   ├── hittest.h, hittest.cpp
│   └── interfaces.h
├── input/
│   ├── canvas_input.h, canvas_input.cpp
│   └── input_types.h
├── window/
│   ├── window_manager.h
│   └── blueprint_window.h
├── viewport/
│   └── viewport.h, viewport.cpp
├── wires/
│   └── hittest.h, hittest.cpp
└── router/
    ├── router.h
    ├── algorithm.h
    ├── path.h
    ├── grid.h
    └── crossings.h
```

---

*Report generated by static code analysis. Recommendations should be validated against actual runtime behavior and user requirements.*
