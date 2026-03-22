# Phase 8: Cleanup -- Remove Old Code

## Goal

Remove the old blueprint system (`Blueprint`, `FlatBlueprint`, `TypeDefinition` in `src/editor/data/`) and redirect all consumers to use `bp2::` types. This is the final phase where we eliminate technical debt.

**This phase is destructive.** After this phase, the old types no longer exist. All tests must pass using only `bp2::` types.

## Files To Delete

```
src/editor/data/blueprint.h          <- Old Blueprint class
src/editor/data/blueprint.cpp        <- Implementation
src/editor/data/flat_blueprint.h     <- Old FlatBlueprint struct
src/editor/data/flat_blueprint.cpp   <- (if exists)
src/editor/data/type_def_convert.h   <- TypeDefinition conversion
src/editor/data/type_def_convert.cpp <- Implementation
```

## Files To Modify

```
src/editor/document.h                <- Replace Blueprint with bp2::EditorModel
src/editor/document.cpp               <- Update to use bp2 types
src/editor/commands/commands.h        <- Commands operate on bp2::EditorModel
src/editor/commands/commands.cpp      <- Implementation
src/editor/visual/scene_mutations.h   <- Scene mutations use bp2 types
src/editor/visual/scene_mutations.cpp
src/editor/visual/persist.h           <- Persistence uses bp2::BlueprintCodec
src/editor/visual/persist.cpp
src/json_parser/json_parser.h         <- Remove old to_simulator_json path
src/json_parser/json_parser.cpp
tests/test_data.cpp                   <- Rewrite to use bp2 types
tests/test_blueprint_v2.cpp           <- Delete (was testing old v2 format)
tests/test_blueprint_integration.cpp  <- Rewrite to use bp2 types
... and many more test files
```

## Prerequisites

- Phase 7 complete (EditorModel, Bridge layer working)
- All editor features working with new types via bridge

## Step-by-Step Instructions

### Step 8.1: Audit all consumers of old types

**Run grep to find all files referencing old types:**

```bash
cd /Users/vladimir/an24_cpp
grep -r "struct Blueprint" --include="*.h" --include="*.cpp" src/
grep -r "FlatBlueprint" --include="*.h" --include="*.cpp" src/
grep -r "TypeDefinition" --include="*.h" --include="*.cpp" src/
grep -r "#include.*blueprint.h" --include="*.h" --include="*.cpp" src/editor/
```

Create a list of all files that need modification. Prioritize:
1. Core data types (document.h, commands.h)
2. Persistence layer (persist.h)
3. Visual layer (scene_mutations.h)
4. Tests

### Step 8.2: Replace Blueprint in Document with bp2::EditorModel

**Before** (in `src/editor/document.h`):

```cpp
#include "editor/data/blueprint.h"

struct Document {
    Blueprint blueprint;
    // ...
};
```

**After**:

```cpp
#include "blueprint_v2/editor_model/editor_model.h"

struct Document {
    ui::StringInterner interner;
    bp2::EditorModel model;
    
    bp2::Blueprint const& blueprint() const { return model.current(); }
    // ...
};
```

Update all callers of `doc.blueprint` to use `doc.blueprint()` accessor.

**Write test first:**

```cpp
TEST(Document, UsesEditorModel) {
    Document doc;
    EXPECT_TRUE(doc.blueprint().nodes().empty());
    
    ui::StringInterner& interner = doc.interner;
    bp2::Blueprint::Node node;
    node.id = interner.intern("test");
    node.type = interner.intern("Battery");
    
    doc.model.add_node(std::move(node));
    EXPECT_EQ(doc.blueprint().nodes().size(), 1u);
}
```

Build. Confirm fail. Update Document. Run. Pass.

### Step 8.3: Replace commands to operate on EditorModel

**Before** (in `src/editor/commands/commands.h`):

```cpp
void add_node(Blueprint& bp, Node node);
void remove_node(Blueprint& bp, InternedId id);
// ...
```

**After**:

```cpp
void add_node(bp2::EditorModel& model, bp2::Blueprint::Node node);
void remove_node(bp2::EditorModel& model, ui::InternedId id);
// ...
```

Update implementations in `commands.cpp` to call `model.add_node()` etc.

**Key insight:** Commands now automatically support undo/redo because `EditorModel` manages history.

### Step 8.4: Replace persistence with bp2::BlueprintCodec

**Before** (in `src/editor/visual/persist.cpp`):

```cpp
std::string save_blueprint(Blueprint const& bp) {
    return bp.serialize();  // Old JSON format
}

Blueprint load_blueprint(std::string const& json) {
    auto bp = Blueprint::deserialize(json);
    return bp.value();
}
```

**After**:

```cpp
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/bridge/blueprint_bridge.h"

std::string save_blueprint(bp2::Blueprint const& bp,
                          ui::StringInterner& interner,
                          bp2::PathArena& arena) {
    return bp2::BlueprintCodec::encode(bp, interner, arena);
}

bp2::Blueprint load_blueprint(std::string const& json,
                              ui::StringInterner& interner,
                              bp2::TypeRegistry& registry) {
    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(json, interner, registry, &err);
    if (!bp) {
        // Try old format via bridge
        auto flat = parse_flat_blueprint(json);
        if (flat) {
            return bp2::BlueprintBridge::from_flat(*flat, interner);
        }
        throw std::runtime_error("Failed to parse blueprint: " + err.message);
    }
    return *bp;
}
```

**Backwards compatibility note:** The loader attempts new format first, falls back to old format via bridge. After Phase 8 is complete, all saved files should be in new format, so the fallback can be removed in a future cleanup.

### Step 8.5: Update scene_mutations to use bp2 types

The `scene_mutations.cpp` file syncs visual nodes/wires with the data model. Update it to:

1. Use `bp2::Blueprint::Node` instead of `Node`
2. Use `bp2::Blueprint::Wire` instead of `Wire`
3. Use `bp2::Path` for port addressing instead of string concatenation

**Before:**

```cpp
void sync_node_to_visual(Blueprint const& bp, Node const& node, visual::Scene& scene) {
    std::string node_id(interner.resolve(node.id));
    // ...
}
```

**After:**

```cpp
void sync_node_to_visual(bp2::Blueprint const& bp,
                         bp2::Blueprint::Node const& node,
                         ui::StringInterner& interner,
                         visual::Scene& scene) {
    std::string node_id(interner.resolve(node.id));
    // ...
}
```

### Step 8.6: Remove the bridge layer (optional, can defer)

Once all consumers are updated and all saved files are in the new format, the bridge layer (`BlueprintBridge`) is no longer needed. It can be:

1. **Kept** for backwards compatibility with user files (recommended initially)
2. **Removed** in a future cleanup once all files are migrated

To remove:
- Delete `src/blueprint_v2/bridge/blueprint_bridge.h`
- Delete `src/blueprint_v2/bridge/blueprint_bridge.cpp`
- Delete `tests/blueprint_v2/test_bridge.cpp`
- Update `src/blueprint_v2/CMakeLists.txt` to remove bridge files

### Step 8.7: Delete old type files

Once all tests pass with the new system:

```bash
rm src/editor/data/blueprint.h
rm src/editor/data/blueprint.cpp
rm src/editor/data/flat_blueprint.h
# ... etc
```

**IMPORTANT:** Run full test suite after each deletion:

```bash
cd build && ctest --output-on-failure
```

### Step 8.8: Update tests to use bp2 types

Many existing tests use the old `Blueprint` type directly. These must be rewritten:

**Old test:**

```cpp
TEST(BlueprintTest, AddNode) {
    Blueprint bp;
    Node n;
    n.id = "bat1";
    n.type = "Battery";
    bp.add_node(std::move(n));
    EXPECT_EQ(bp.nodes.size(), 1u);
}
```

**New test:**

```cpp
TEST(BlueprintTest, AddNode) {
    ui::StringInterner interner;
    bp2::EditorModel model;
    
    bp2::Blueprint::Node n;
    n.id = interner.intern("bat1");
    n.type = interner.intern("Battery");
    
    model.add_node(std::move(n));
    EXPECT_EQ(model.current().nodes().size(), 1u);
}
```

**Strategy for test migration:**
1. Tests that test old types directly -> **delete** (they test code we're removing)
2. Tests that test behavior through old types -> **rewrite** to use `bp2::` types
3. Integration tests -> **update** to use bridge if loading old files, or use new codec

### Step 8.9: Update AGENTS.md

Add instructions for the new `bp2::` types to `AGENTS.md`:

```markdown
## Blueprint V2 Types

All blueprint manipulation uses `bp2::` namespace types:

- `bp2::Blueprint` - Canonical blueprint type (immutable value)
- `bp2::EditorModel` - Editor wrapper with undo/redo
- `bp2::Path` / `bp2::PathArena` - Typed hierarchical paths
- `bp2::Interface` / `bp2::PortDescriptor` - Port definitions
- `bp2::TypeRegistry` - Injectable component registry
- `bp2::BlueprintCodec` - JSON serialization
- `bp2::Flattener` - Hierarchy to flat netlist

All new code should use these types. The old `Blueprint` class in `src/editor/data/` is deprecated and will be removed.
```

## Final Verification

```bash
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest --output-on-failure
```

All tests pass. No references to old types remain.

## Files Deleted This Phase

```
src/editor/data/blueprint.h
src/editor/data/blueprint.cpp
src/editor/data/flat_blueprint.h
src/editor/data/type_def_convert.h
src/editor/data/type_def_convert.cpp
tests/test_blueprint_v2.cpp        (old v2 format tests, not bp2)
```

## Files Modified This Phase

- `src/editor/document.h` - Use bp2::EditorModel
- `src/editor/document.cpp`
- `src/editor/commands/commands.h` - Commands on EditorModel
- `src/editor/commands/commands.cpp`
- `src/editor/visual/persist.h` - Use bp2::BlueprintCodec
- `src/editor/visual/persist.cpp`
- `src/editor/visual/scene_mutations.h`
- `src/editor/visual/scene_mutations.cpp`
- `AGENTS.md` - Add bp2 documentation
- `tests/CMakeLists.txt` - Update test targets
- Many test files rewritten

## Success Criteria

After Phase 8:
1. All tests pass
2. No old `Blueprint` or `FlatBlueprint` types in codebase
3. All blueprint files saved in new JSON format (version "3.0")
4. Editor fully functional with undo/redo via `bp2::EditorModel`
5. Simulator uses `bp2::Flattener` to produce netlists

## Rollback Plan

If Phase 8 reveals critical issues:
1. Restore deleted files from git
2. Revert `document.h`, `commands.h`, `persist.h` changes
3. Keep bridge layer for compatibility
4. Investigate and fix issues, then retry Phase 8

Phase 8 can be done incrementally -- migrate one subsystem at a time, running tests after each change.
