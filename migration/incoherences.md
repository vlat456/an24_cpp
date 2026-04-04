# Incoherences Between BLUEPRINT_ARCHITECTURE_V2.md and Phase Files

This document lists inconsistencies, gaps, and ambiguities found when comparing the architecture specification with the migration phase files.

---

## 1. Path Size Claim is Incorrect

**Architecture (Part II, 2.2):**
> "Paths are **8 bytes** (kind + segment + index)"

**Reality:**
```cpp
class Path {
    Kind kind_;           // uint8_t = 1 byte
    InternedId segment_;  // const char* pointer = 8 bytes (64-bit)
    uint32_t parent_idx_; // 4 bytes
};
```
With alignment padding, `Path` is approximately **16-24 bytes**, not 8 bytes.

**Fix:** Update architecture doc to reflect actual size, or redesign Path to use indices for everything (store segment in arena too).

---

## 2. Missing PathResolver Implementation

**Architecture (Part III, 3.2):**
```cpp
class PathResolver {
    std::optional<ResolvedPort> resolve(Path const& path, Blueprint const& root, TypeRegistry const& registry) const;
    bool can_connect(Path const& source, Path const& target, Blueprint const& root, TypeRegistry const& registry) const;
};
```

**Status:** RESOLVED ✓

Implemented in:
- `src/blueprint_v2/validation/path_resolver.h`
- `src/blueprint_v2/validation/path_resolver.cpp`
- Tests: `tests/blueprint_v2/test_validation.cpp` (`PathResolver.*`)

**Impact:** Wire validation (Part VIII) depends on this. Without it, we can't validate boundary crossing rules.

**Fix:** Add `PathResolver` to Phase 2 or create a new Phase 2.5.

---

## 3. Missing WireValidator Implementation

**Architecture (Part VIII, 8.2):**
```cpp
class WireValidator {
    static Result validate(Blueprint::Wire const& wire, Blueprint const& bp, TypeRegistry const& registry);
};
```

**Status:** RESOLVED ✓

Implemented in:
- `src/blueprint_v2/validation/wire_validator.h`
- `src/blueprint_v2/validation/wire_validator.cpp`
- Tests: `tests/blueprint_v2/test_validation.cpp` (`WireValidator.*`)

**Impact:** The invariants I4 (wire validity) cannot be enforced at construction time without this.

**Fix:** Add `WireValidator` to Phase 3 (Blueprint) or Phase 6 (Flattener).

---

## 4. Direction Enum Not Defined in Architecture

**Architecture (Part II, 2.3):**
```cpp
struct PortDescriptor {
    Direction direction;  // Input, Output, InOut
};
```

**Phase Files:** Phase 2 references `Direction` but the enum isn't defined in the architecture doc. It's assumed to come from `json_parser.h`.

**Fix:** Either:
1. Define `Direction` in the architecture doc explicitly
2. Reference the existing enum in `json_parser.h` and note the dependency

---

## 5. Params Key Type Inconsistency

**Architecture (Part II, 2.4):**
```cpp
struct Node {
    std::unordered_map<InternedId, float> params;  // Key is InternedId
};
```

**Phase Files (Phase 3):**
```cpp
struct Node {
    std::unordered_map<std::string, float> params;  // Key is std::string
};
```

**Impact:** 
- Architecture uses interned keys for O(1) lookup
- Phase files use string keys for JSON compatibility
- Bridge layer will need to convert between these

**Fix:** Decide on one approach. If using `std::string` keys, update architecture. If using `InternedId`, update phase files.

---

## 6. JSON Version Mismatch (Intentional)

**Architecture (Part V, 5.1):**
```json
"version": "2.0"
```

**Phase 5:**
```json
"version": "3.0"
```

**Status:** This is **intentional** - Phase 5 notes that version 3.0 distinguishes from the old FlatBlueprint v2 format. No fix needed, but architecture should be updated to reflect the final version number.

---

## 7. EditorModel Derived Indices Missing

**Architecture (Part VI, 6.1):**
```cpp
class EditorModel {
    mutable struct Indices {
        std::unordered_map<Rect, std::vector<InternedId>> spatial_index;
        std::unordered_set<std::pair<Path, Path>, PairHash> wire_set;
        bool valid = false;
    } indices_;
};
```

**Status:** PARTIALLY RESOLVED

`bp2::EditorModel` now has derived-index style queries:
- `nodes_in_rect(Rect)`
- `wire_exists(Path, Path)`

Implementation uses a cached map/set index in `editor_model.h/.cpp`.
A true spatial tree is still optional future optimization.

**Impact:** 
- `nodes_in_rect(Rect r)` cannot work without spatial_index
- Wire deduplication queries unavailable

**Fix:** Add spatial index implementation to Phase 7 or defer to Phase 8 (cleanup).

---

## 8. Missing Blueprint Methods

**Architecture (Part II, 2.4) defines these methods:**
```cpp
Blueprint clone(InternedId new_id) const;
std::vector<std::pair<Path, PortDescriptor>> all_ports(PathArena& arena) const;
```

**Status:** RESOLVED ✓

Implemented in:
- `src/blueprint_v2/blueprint/blueprint.h`
- `src/blueprint_v2/blueprint/blueprint.cpp`

**Impact:**
- `clone()` is needed for bake-in (Phase 7)
- `all_ports()` is needed for validation

**Fix:** Add these methods to Phase 3 or Phase 4.

---

## 9. Missing bake_all() Function

**Architecture (Part VII, 7.3):**
```cpp
Blueprint bake_all(Blueprint const& bp, TypeRegistry const& registry);
```

**Status:** RESOLVED ✓

Implemented in:
- `src/blueprint_v2/bake/bake_ops.h`
- `src/blueprint_v2/bake/bake_ops.cpp`
- Includes `bake_nested`, `try_unbake`, and recursive `bake_all`
- Tests: `tests/blueprint_v2/test_bake.cpp`

**Fix:** Add `bake_all()` to Phase 7.

---

## 10. TypeRegistry Entry Structure Mismatch

**Architecture (Part II, 2.5):**
```cpp
struct Entry {
    InternedId type_id;
    Interface iface;
    std::string description;
    bool is_blueprint;
    // Note: No Blueprint storage shown
};
```

**Architecture (Part IV, 4.3) implies:**
```cpp
bp_to_visit = entry->blueprint;  // Where does this come from?
```

**Phase 4:** Needs clarification on whether `Entry` stores a `Blueprint` pointer/reference for blueprint types.

**Fix:** Add `Blueprint const* blueprint` to `Entry` struct, or clarify how blueprints are stored.

---

## 11. Interface Port Representation

**Architecture:** Interface ports are declared in JSON:
```json
"interface": [
    {"name": "v_in", "domain": "Electrical", "direction": "Input"}
]
```

**Old FlatBlueprint:**
```cpp
std::map<std::string, FlatPort> exposes;
```

**Bridge (Phase 7):** The bridge needs to map between old `exposes` map and new `Interface`, but this mapping isn't explicitly documented.

**Fix:** Add explicit mapping rules to Phase 7 bridge section.

---

## 12. FlatNetlist::port_to_signal Construction

**Architecture (Part IV, 4.1):**
```cpp
struct FlatNetlist {
    std::unordered_map<Path, SignalIndex> port_to_signal;
};
```

**Phase 6:** Shows this map being built in a post-processing step after `visit_blueprint`. This is correct but should be explicit in the architecture.

**Status:** Minor - implementation detail. No fix needed.

---

## 13. InternedId / StringInterner vs SymbolTable

**Architecture (Part II, 2.1):**
```cpp
class InternedId { ... };
class SymbolTable {
    static InternedId intern(std::string_view s);
};
```

**Phase Files:** Use existing `ui::InternedId` and `ui::StringInterner` from `src/ui/core/interned_id.h`.

**Status:** This is **correct** - the phase files adapt the architecture to use existing code. No fix needed, but architecture could note this dependency.

---

## 14. Nested::inline_def Storage Type

**Architecture (Part II, 2.4):**
```cpp
struct Nested {
    std::optional<Blueprint> inline_def;
};
```

**Phase 3:**
```cpp
struct Nested {
    std::optional<Blueprint> inline_def;  // or std::unique_ptr<Blueprint>?
};
```

**Question:** `std::optional<Blueprint>` requires Blueprint to be copyable/movable. If Blueprint is large, this may be inefficient. `std::unique_ptr<Blueprint>` is more efficient but requires explicit allocation.

**Fix:** Decide and be consistent. Recommend `std::unique_ptr<Blueprint>` for efficiency.

---

## 15. Path Parse String Format

**Architecture (Part III, 3.1):**
```
/battery1:v_out      -> Port "v_out" on node "battery1"
/sub_circuit1/resistor1:in  -> Port on nested node
```

**Question:** How are interface ports represented? Architecture shows `/:v_in` but this is ambiguous - is the empty segment before `:` a node or the root?

**Fix:** Clarify in architecture:
- `/:port_name` -> Interface port on root blueprint
- `/nested_id:port_name` -> Interface port on nested instance
- `/nested_id/node_id:port_name` -> Port on node inside nested

---

## 16. InvariantChecker Not Implemented

**Architecture (Part IX):** Defines 6 categories of invariants but no `InvariantChecker` class is specified or implemented.

**Status:** RESOLVED ✓

Implemented in:
- `src/blueprint_v2/validation/invariant_checker.h`
- `src/blueprint_v2/validation/invariant_checker.cpp`
- `Blueprint::validate(registry, arena)` delegates to invariant checker
- Tests: `tests/blueprint_v2/test_validation.cpp` (`BlueprintValidate.*`)

**Fix:** Either:
1. Add `Blueprint::validate()` method (mentioned in architecture) to Phase 3
2. Create separate `InvariantChecker` class in Phase 5 or 6

---

## Resolved Decisions (2024-03-22)

| Issue | Decision |
|-------|----------|
| Params key type | **InternedId keys** - O(1) lookup, requires interning during codec decode |
| Direction enum | **Define in bp2:: namespace** - self-contained, no json_parser dependency |
| Nested::inline_def | **unique_ptr<Blueprint>** - efficient, handles incomplete type |
| Path size | **~12 bytes** (1 byte kind + 4 byte InternedId + 4 byte parent_idx + padding) |

## Summary of Required Fixes

| Priority | Issue | Phase to Fix |
|----------|-------|--------------|
| High | Missing PathResolver | **RESOLVED** |
| High | Missing WireValidator | **RESOLVED** |
| ~~High~~ | ~~Params key type~~ | **RESOLVED: InternedId** |
| Medium | Missing clone()/all_ports() | **RESOLVED** |
| Medium | Missing bake_all() | **RESOLVED** |
| Medium | EditorModel spatial_index | **PARTIAL (lightweight index done)** |
| Medium | TypeRegistry Entry needs Blueprint* | Fix Phase 4 |
| ~~Low~~ | ~~Path size claim~~ | **RESOLVED: ~12 bytes (ui::InternedId is uint32_t)** |
| Low | JSON version (2.0 vs 3.0) | Update architecture doc |
| ~~Low~~ | ~~Direction enum definition~~ | **RESOLVED: Define in bp2::** |
| ~~Low~~ | ~~Nested::inline_def storage~~ | **RESOLVED: unique_ptr** |
