# FlatBlueprint Refactoring Plan

## Goal

Simplify serialization architecture:
1. Rename `v2::BlueprintV2` → `FlatBlueprint` (clear naming)
2. Remove `v2` namespace entirely
3. Shrink `persist.cpp` from 833 → ~50 lines (file I/O only)
4. Move serialization logic into `data/blueprint.cpp`

## Naming Convention

| Old | New | Purpose |
|-----|-----|---------|
| `v2::BlueprintV2` | `FlatBlueprint` | Flat DTO for JSON serialization |
| `v2::NodeV2` | `FlatNode` | Node in flat format |
| `v2::WireV2` | `FlatWire` | Wire in flat format |
| `v2::ContentV2` | `FlatContent` | Content metadata |
| `v2::NodeColorV2` | `FlatColor` | RGBA color |
| `v2::ViewportV2` | `FlatViewport` | Viewport state |
| `v2::SubBlueprintV2` | `FlatSubBlueprint` | Nested blueprint reference |
| `v2::MetaV2` | `FlatMeta` | Metadata |
| `v2::ExposedPort` | `FlatPort` | (already good, just move out of v2) |
| `v2::ParamDef` | `FlatParam` | Parameter definition |

## Architecture After Refactoring

```
src/
├── data/
│   ├── blueprint.h              # Blueprint (runtime)
│   ├── blueprint.cpp            # Blueprint + serialization methods
│   ├── flat_blueprint.h         # FlatBlueprint, FlatNode, FlatWire, etc.
│   └── flat_blueprint.cpp       # FlatBlueprint JSON parse/serialize
│
├── editor/visual/scene/
│   └── persist.cpp              # ONLY file I/O (~50 lines)
│       ├── save_blueprint_to_file(Blueprint, path)
│       └── load_blueprint_from_file(path) → Blueprint
│
└── jit_solver/
    └── export_sim.cpp           # Simulator JSON export (or in simulation.cpp)
        └── blueprint_to_simulator_json(Blueprint) → string
```

## Files to Modify

### Layer 1 — Rename types (mechanical)

| File | Action |
|------|--------|
| `src/v2/blueprint_v2.h` | Rename to `src/editor/data/flat_blueprint.h`, rename all types |
| `src/v2/blueprint_v2.cpp` | Rename to `src/editor/data/flat_blueprint.cpp`, update all type names |
| `src/v2/convert.h` | Rename to `src/editor/data/type_def_convert.h` |
| `src/v2/convert.cpp` | Rename to `src/editor/data/type_def_convert.cpp` |
| Delete `src/v2/` directory | After moving files |

### Layer 2 — Update includes and references

| Pattern | Replace |
|---------|---------|
| `#include "v2/blueprint_v2.h"` | `#include "data/flat_blueprint.h"` |
| `#include "v2/convert.h"` | `#include "data/type_def_convert.h"` |
| `v2::BlueprintV2` | `FlatBlueprint` |
| `v2::NodeV2` | `FlatNode` |
| `v2::WireV2` | `FlatWire` |
| `v2::ContentV2` | `FlatContent` |
| `v2::NodeColorV2` | `FlatColor` |
| `v2::ViewportV2` | `FlatViewport` |
| `v2::SubBlueprintV2` | `FlatSubBlueprint` |
| `v2::MetaV2` | `FlatMeta` |
| `namespace v2 {` | *(remove)* |
| `} // namespace v2` | *(remove)* |

### Layer 3 — Move serialization logic

**From `persist.cpp` to `data/blueprint.cpp`:**

```cpp
// In blueprint.cpp
std::string Blueprint::serialize() const {
    FlatBlueprint flat = to_flat();
    return serialize_flat_blueprint(flat);
}

std::optional<Blueprint> Blueprint::deserialize(const std::string& json) {
    auto flat = parse_flat_blueprint(json);
    if (!flat) return std::nullopt;
    return from_flat(*flat);
}

std::string Blueprint::to_simulator_json() const {
    // Current blueprint_to_json() logic
}
```

**New `persist.cpp` (minimal):**

```cpp
#include "data/blueprint.h"
#include <fstream>
#include <sstream>

bool save_blueprint_to_file(const Blueprint& bp, const char* path) {
    std::ofstream file(path);
    if (!file) return false;
    file << bp.serialize();
    return true;
}

std::optional<Blueprint> load_blueprint_from_file(const char* path) {
    std::ifstream file(path);
    if (!file) return std::nullopt;
    std::stringstream buf;
    buf << file.rdbuf();
    return Blueprint::deserialize(buf.str());
}
```

### Layer 4 — Update callers

| File | Change |
|------|--------|
| `src/jit_solver/simulator.cpp` | `blueprint_to_json(bp)` → `bp.to_simulator_json()` |
| `src/editor/simulation.cpp` | Same |
| `src/editor/visual/scene/persist.cpp` | Delete most code, keep only file I/O |
| Tests using `v2::` | Update to new names |

### Layer 5 — Update CMakeLists.txt

- Remove `src/v2/` from sources
- Add `src/editor/data/flat_blueprint.cpp`
- Add `src/editor/data/type_def_convert.cpp`

### Layer 6 — Update documentation

| File | Action |
|------|--------|
| `AGENTS.md` | Update references to FlatBlueprint |
| `V2_IMPL_PLAN.md` | Archive or update |
| Other .md files | Update code examples |

## Estimated Scope

- **~15 files** modified
- **~200 lines** moved (not deleted)
- **persist.cpp: 833 → ~50 lines** (major win)
- **4 files deleted** (old v2/ directory)

## Build & Test After Each Layer

```bash
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

## Known Risks

1. **Test coverage** — Tests for v2 format need renaming, verify they still pass
2. **JSON compatibility** — Output format must remain identical (rename is internal only)
3. **Library loading** — `type_def_convert.cpp` handles library JSON, verify still works

## Execution Order

1. ✅ Create plan (this file)
2. ⬜ Layer 1: Rename v2/*.h/*.cpp files and types
3. ⬜ Layer 2: Update all includes and qualified names
4. ⬜ Layer 3: Move serialization to Blueprint class
5. ⬜ Layer 4: Update callers
6. ⬜ Layer 5: Update CMakeLists.txt
7. ⬜ Layer 6: Update documentation
8. ⬜ Verify: Build + all 1344 tests pass
