# Phase 7: EditorModel and Bridge Layer

Historical note: this phase description references legacy bp2 registry APIs that were removed; canonical registry lives in json_parser.

## Goal

Create `bp2::EditorModel` which wraps a `Blueprint` with undo/redo support and derived indices. Also create the temporary `BlueprintBridge` that converts between old types (`FlatBlueprint`, `TypeDefinition`) and new `bp2::Blueprint` for gradual migration.

This is the **integration phase** -- we finally touch the old code in `src/editor/data/` and provide adapters.

## Files To Create

```
src/blueprint_v2/editor_model/editor_model.h       <- EditorModel class
src/blueprint_v2/editor_model/editor_model.cpp     <- implementation
src/blueprint_v2/bridge/blueprint_bridge.h         <- Bridge conversion functions
src/blueprint_v2/bridge/blueprint_bridge.cpp       <- implementation
tests/blueprint_v2/test_editor_model.cpp           <- EditorModel tests
tests/blueprint_v2/test_bridge.cpp                 <- Bridge conversion tests
```

## Files To Modify (temporarily, until Phase 8)

```
src/editor/document.h                              <- Use bp2::EditorModel instead of old Blueprint
src/editor/commands/commands.h                     <- Command mutations go through EditorModel
```

## Prerequisites

- Phases 1-6 complete (Path, Interface, Blueprint, Registry, Codec, Flattener)
- Old code in `src/editor/data/` still exists

## Step-by-Step Instructions

### Step 7.1: CMake setup

1. Add new files to `src/blueprint_v2/CMakeLists.txt`:

```cmake
add_library(blueprint_v2 STATIC
    path/path.cpp
    interface/interface.cpp
    blueprint/blueprint.cpp
    registry/type_registry.cpp
    codec/blueprint_codec.cpp
    flattener/flattener.cpp
    editor_model/editor_model.cpp
    bridge/blueprint_bridge.cpp
)
```

2. Add test targets in `tests/CMakeLists.txt`:

```cmake
add_executable(bp2_editor_model_tests
    blueprint_v2/test_editor_model.cpp
)
target_include_directories(bp2_editor_model_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(bp2_editor_model_tests PRIVATE
    blueprint_v2
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(bp2_editor_model_tests)

add_executable(bp2_bridge_tests
    blueprint_v2/test_bridge.cpp
)
target_include_directories(bp2_bridge_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor/data
)
target_link_libraries(bp2_bridge_tests PRIVATE
    blueprint_v2
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(bp2_bridge_tests)
```

3. Create placeholder files:
   - `src/blueprint_v2/editor_model/editor_model.h`
   - `src/blueprint_v2/editor_model/editor_model.cpp`
   - `src/blueprint_v2/bridge/blueprint_bridge.h`
   - `src/blueprint_v2/bridge/blueprint_bridge.cpp`
   - `tests/blueprint_v2/test_editor_model.cpp`
   - `tests/blueprint_v2/test_bridge.cpp`

4. Build. Placeholders pass.

### Step 7.2: EditorModel -- construction, current()

**Write test first** in `test_editor_model.cpp`:

```cpp
#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/blueprint/blueprint.h"

TEST(EditorModel, EmptyByDefault) {
    bp2::EditorModel model;
    EXPECT_TRUE(model.current().nodes().empty());
}

TEST(EditorModel, ConstructWithBlueprint) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test"));
    bp2::EditorModel model(bp);
    EXPECT_EQ(interner.resolve(model.current().id()), "test");
}
```

Build. Confirm fail.

**Write production code** in `editor_model.h`:

```cpp
#pragma once
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include <vector>
#include <memory>

namespace bp2 {

class EditorModel {
public:
    EditorModel() = default;
    explicit EditorModel(Blueprint initial);

    Blueprint const& current() const { return current_; }

    // === Commands (return true if changed) ===
    bool add_node(Blueprint::Node node);
    bool remove_node(ui::InternedId id);
    bool add_wire(Blueprint::Wire wire);
    bool remove_wire(ui::InternedId id);
    bool add_nested(Blueprint::Nested nested);
    bool remove_nested(ui::InternedId id);

    // === History ===
    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }
    void undo();
    void redo();
    void push_checkpoint();

    size_t undo_depth() const { return undo_stack_.size(); }
    size_t redo_depth() const { return redo_stack_.size(); }

private:
    Blueprint current_;
    std::vector<Blueprint> undo_stack_;
    std::vector<Blueprint> redo_stack_;
    size_t max_history_ = 100;
};

} // namespace bp2
```

Implement in `editor_model.cpp`:

```cpp
#include "editor_model.h"

namespace bp2 {

EditorModel::EditorModel(Blueprint initial)
    : current_(std::move(initial)) {}

bool EditorModel::add_node(Blueprint::Node node) {
    if (current_.find_node(node.id)) return false;
    push_checkpoint();
    current_ = current_.with_node(std::move(node));
    return true;
}

bool EditorModel::remove_node(ui::InternedId id) {
    if (!current_.find_node(id)) return false;
    push_checkpoint();
    current_ = current_.without_node(id);
    return true;
}

bool EditorModel::add_wire(Blueprint::Wire wire) {
    if (current_.find_wire(wire.id)) return false;
    push_checkpoint();
    current_ = current_.with_wire(std::move(wire));
    return true;
}

bool EditorModel::remove_wire(ui::InternedId id) {
    if (!current_.find_wire(id)) return false;
    push_checkpoint();
    current_ = current_.without_wire(id);
    return true;
}

bool EditorModel::add_nested(Blueprint::Nested nested) {
    if (current_.find_nested(nested.id)) return false;
    push_checkpoint();
    current_ = current_.with_nested(std::move(nested));
    return true;
}

bool EditorModel::remove_nested(ui::InternedId id) {
    if (!current_.find_nested(id)) return false;
    push_checkpoint();
    current_ = current_.without_nested(id);
    return true;
}

void EditorModel::push_checkpoint() {
    redo_stack_.clear();
    if (undo_stack_.size() >= max_history_) {
        undo_stack_.erase(undo_stack_.begin());
    }
    undo_stack_.push_back(current_);
}

void EditorModel::undo() {
    if (!can_undo()) return;
    redo_stack_.push_back(std::move(current_));
    current_ = std::move(undo_stack_.back());
    undo_stack_.pop_back();
}

void EditorModel::redo() {
    if (!can_redo()) return;
    undo_stack_.push_back(std::move(current_));
    current_ = std::move(redo_stack_.back());
    redo_stack_.pop_back();
}

} // namespace bp2
```

Build. Run. Pass.

### Step 7.3: EditorModel -- undo/redo tests

**Write test first:**

```cpp
TEST(EditorModel, UndoRestoresPreviousState) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");

    model.add_node(std::move(node));
    EXPECT_EQ(model.current().nodes().size(), 1u);

    model.undo();
    EXPECT_EQ(model.current().nodes().size(), 0u);
}

TEST(EditorModel, RedoAfterUndo) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");

    model.add_node(std::move(node));
    model.undo();
    EXPECT_FALSE(model.can_redo() == false);  // Should be able to redo
    EXPECT_EQ(model.redo_depth(), 1u);

    model.redo();
    EXPECT_EQ(model.current().nodes().size(), 1u);
}

TEST(EditorModel, NewActionClearsRedoStack) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node n1, n2;
    n1.id = interner.intern("n1");
    n1.type = interner.intern("Battery");
    n2.id = interner.intern("n2");
    n2.type = interner.intern("Resistor");

    model.add_node(std::move(n1));
    model.undo();

    // Add a different node -- redo stack should clear
    model.add_node(std::move(n2));
    EXPECT_FALSE(model.can_redo());
    EXPECT_EQ(model.redo_depth(), 0u);
}
```

Build. Run. Pass.

### Step 7.4: EditorModel -- duplicate ID rejection

**Write test first:**

```cpp
TEST(EditorModel, DuplicateNodeRejected) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node n1, n2;
    n1.id = interner.intern("same_id");
    n1.type = interner.intern("Battery");
    n2.id = interner.intern("same_id");
    n2.type = interner.intern("Resistor");

    EXPECT_TRUE(model.add_node(std::move(n1)));
    EXPECT_FALSE(model.add_node(std::move(n2)));  // Duplicate rejected
    EXPECT_EQ(model.current().nodes().size(), 1u);
    EXPECT_EQ(model.undo_depth(), 1u);  // Only one checkpoint
}
```

Build. Run. Pass.

### Step 7.5: EditorModel -- update_node, update_nested positions

**Write test first:**

```cpp
TEST(EditorModel, UpdateNodePosition) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");
    node.x = 100.0f;
    node.y = 200.0f;
    model.add_node(std::move(node));

    // Update via remove + add with same ID different position
    // OR provide update_node() method
    model.update_node_position(interner.intern("n1"), 300.0f, 400.0f);

    auto* found = model.current().find_node(interner.intern("n1"));
    ASSERT_NE(found, nullptr);
    EXPECT_FLOAT_EQ(found->x, 300.0f);
    EXPECT_FLOAT_EQ(found->y, 400.0f);
}
```

Build. Confirm fail.

**Write production code.** Add to `EditorModel`:

```cpp
    bool update_node_position(ui::InternedId id, float x, float y);
```

Implement:

```cpp
bool EditorModel::update_node_position(ui::InternedId id, float x, float y) {
    auto* node = const_cast<Blueprint::Node*>(current_.find_node(id));
    if (!node) return false;
    push_checkpoint();
    node->x = x;
    node->y = y;
    return true;
}
```

**Note:** This mutates in place, which breaks immutability. Better approach: use `without_node` + `with_node`:

```cpp
bool EditorModel::update_node_position(ui::InternedId id, float x, float y) {
    auto const* existing = current_.find_node(id);
    if (!existing) return false;

    Blueprint::Node updated = *existing;
    updated.x = x;
    updated.y = y;

    push_checkpoint();
    current_ = current_.without_node(id).with_node(std::move(updated));
    return true;
}
```

Build. Run. Pass.

### Step 7.5a: EditorModel -- wire validation on add

The architecture defines wire invariants (I4): domain match, direction compatibility, no self-loops, at most one hierarchy boundary crossed. Rather than a standalone `WireValidator` class, enforce these rules inside `EditorModel::add_wire()`.

**Write test first:**

```cpp
TEST(EditorModel, RejectsSelfLoopWire) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::EditorModel model;

    bp2::Blueprint::Node n1;
    n1.id = interner.intern("n1");
    n1.type = interner.intern("Battery");
    model.add_node(std::move(n1));

    bp2::Blueprint::Wire w;
    w.id = interner.intern("w_self");
    auto port = arena.make_port(
        arena.make_node(arena.root(), interner.intern("n1")),
        interner.intern("v_out"));
    w.source = port;
    w.target = port;  // self-loop

    EXPECT_FALSE(model.add_wire(std::move(w)));
}
```

Build. Confirm fail (current `add_wire` only checks duplicate ID). Update `add_wire`:

```cpp
bool EditorModel::add_wire(Blueprint::Wire wire) {
    if (current_.find_wire(wire.id)) return false;
    if (wire.source == wire.target) return false;  // self-loop
    push_checkpoint();
    current_ = current_.with_wire(std::move(wire));
    return true;
}
```

Build. Run. Pass.

**Note:** Full wire validation (domain match, direction, boundary rules) requires `TypeRegistry` access. For now we only check the cheapest invariant (self-loop). Full validation can be added to `add_wire` by giving `EditorModel` a `TypeRegistry const*` reference, or by providing a separate `validate_wire()` method. Defer full validation until the editor UI actually wires this up.

### Step 7.5b: EditorModel -- bake_nested and clone

The architecture (Part VII) defines `bake_nested()` and `bake_all()`. Implement single-level bake-in.

**Write test first:**

```cpp
TEST(EditorModel, BakeNestedConvertsReferenceToEmbedded) {
     ui::StringInterner interner;
     TypeRegistry reg = load_type_registry("library/");

    // Create a blueprint to use as the nested type
    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("sub_type"));
    inner = inner.with_interface(bp2::Interface({
        {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
    }));
    reg.register_blueprint(interner.intern("sub_type"), inner.iface(), "test", &inner);

    // Build editor model with a reference-mode nested instance
    bp2::EditorModel model;
    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("sub_type");
    nested.embedded = false;
    nested.iface = inner.iface();
    model.add_nested(std::move(nested));

    // Bake
    EXPECT_TRUE(model.bake_nested(interner.intern("sub1"), reg, interner));

    auto* baked = model.current().find_nested(interner.intern("sub1"));
    ASSERT_NE(baked, nullptr);
    EXPECT_TRUE(baked->embedded);
    EXPECT_NE(baked->inline_def, nullptr);
}
```

Build. Confirm fail.

**Write production code.** Add to `EditorModel`:

```cpp
     bool bake_nested(ui::InternedId id, TypeRegistry const& registry,
                      ui::StringInterner& interner);
```

Implement:

```cpp
bool EditorModel::bake_nested(ui::InternedId id,
                               TypeRegistry const& registry,
                               ui::StringInterner& interner) {
    auto const* nested = current_.find_nested(id);
    if (!nested) return false;
    if (nested->embedded) return false;  // Already embedded

    auto* entry = registry.find(nested->blueprint_id);
    if (!entry || !entry->blueprint) return false;

    Blueprint::Nested baked;
    baked.id = nested->id;
    baked.blueprint_id = {};
    baked.embedded = true;
    baked.inline_def = std::make_unique<Blueprint>(*entry->blueprint);
    baked.iface = nested->iface;
    baked.x = nested->x;
    baked.y = nested->y;

    push_checkpoint();
    current_ = current_.without_nested(id).with_nested(std::move(baked));
    return true;
}
```

Build. Run. Pass.

**Note on `bake_all()` and `unbake_nested()`:** These follow the same pattern. `bake_all()` recursively bakes all reference-mode nested instances. `unbake_nested()` reverses bake-in by searching the registry for a matching blueprint. Implement with TDD when needed by the editor UI. The architecture (Part VII, sections 7.2-7.3) has the pseudocode.

### Step 7.5c: Spatial index and wire_set (deferred)

The architecture defines `spatial_index` (for `nodes_in_rect()` queries) and `wire_set` (for deduplication). These are needed for the visual editor but are **not blocking** for the core pipeline. The old `Blueprint` already has `wire_index_` and `port_occupancy_index_` which handle these concerns during the bridge period.

**When to add:** Implement these when Phase 8 removes the old `Blueprint` and the editor needs to query the new model directly. Add with TDD at that point:
- `spatial_index`: rebuild on `invalidate_indices()`, query via `nodes_in_rect(Rect)`
- `wire_set`: `unordered_set<pair<Path, Path>>` rebuilt from wires, query via `wire_exists(Path, Path)`

### Step 7.6: BlueprintBridge -- from_flat (old FlatBlueprint to new Blueprint)

**Write test first** in `test_bridge.cpp`:

```cpp
#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/bridge/blueprint_bridge.h"
#include "editor/data/flat_blueprint.h"  // old type
#include "blueprint_v2/blueprint/blueprint.h"

TEST(BlueprintBridge, FromFlatEmpty) {
    ui::StringInterner interner;
    FlatBlueprint flat;  // old type, empty

    bp2::Blueprint bp = bp2::BlueprintBridge::from_flat(flat, interner);
    EXPECT_TRUE(bp.nodes().empty());
    EXPECT_TRUE(bp.wires().empty());
}
```

Build. Confirm fail (no `BlueprintBridge`).

**Write production code** in `blueprint_bridge.h`:

```cpp
#pragma once
#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"

// Forward declarations for old types (avoid heavy includes in header)
class FlatBlueprint;
class TypeDefinition;

namespace bp2 {

class BlueprintBridge {
public:
    /// Convert old FlatBlueprint to new Blueprint
    static Blueprint from_flat(FlatBlueprint const& flat,
                               ui::StringInterner& interner);

    /// Convert new Blueprint to old FlatBlueprint (for gradual rollout)
    static FlatBlueprint to_flat(Blueprint const& bp,
                                 ui::StringInterner& interner);

    /// Convert old TypeDefinition to new Blueprint
     static Blueprint from_type_def(TypeDefinition const& td,
                                    ui::StringInterner& interner,
                                    class TypeRegistry& registry);

    /// Convert new Blueprint to old TypeDefinition
    static TypeDefinition to_type_def(Blueprint const& bp,
                                      ui::StringInterner& interner);
};

} // namespace bp2
```

Read the old `FlatBlueprint` definition in `src/editor/data/flat_blueprint.h` to understand the mapping:

```cpp
// FlatBlueprint typically has:
// - std::string id
// - std::vector<NodeInstance> nodes
// - std::vector<WireConnection> wires
// - std::vector<SubBlueprintInstance> sub_instances
```

Implement `from_flat` in `blueprint_bridge.cpp`:

```cpp
#include "blueprint_bridge.h"
#include "editor/data/flat_blueprint.h"
#include "editor/data/node.h"  // for NodeInstance
#include "editor/data/wire.h"  // for WireConnection
#include "blueprint_v2/path/path.h"

namespace bp2 {

Blueprint BlueprintBridge::from_flat(FlatBlueprint const& flat,
                                     ui::StringInterner& interner) {
    Blueprint bp;
    bp = bp.with_id(interner.intern(flat.id));

    PathArena arena(interner);

    // Convert nodes
    for (auto const& fn : flat.nodes) {
        Blueprint::Node node;
        node.id = interner.intern(fn.id);
        node.type = interner.intern(fn.type);
        node.x = fn.x;
        node.y = fn.y;
        node.params = fn.params;  // Assuming std::unordered_map<std::string, float>
        // node.iface must be resolved from registry -- caller does this
        bp = bp.with_node(std::move(node));
    }

    // Convert wires
    for (auto const& fw : flat.wires) {
        Blueprint::Wire wire;
        wire.id = interner.intern(fw.id);

        // Parse old string-based endpoints like "node1:port_name"
        auto src = arena.parse(fw.source);
        auto tgt = arena.parse(fw.target);
        if (src) wire.source = *src;
        if (tgt) wire.target = *tgt;
        wire.domain = fw.domain;

        bp = bp.with_wire(std::move(wire));
    }

    // Convert nested instances
    for (auto const& sub : flat.sub_instances) {
        Blueprint::Nested nested;
        nested.id = interner.intern(sub.id);
        nested.blueprint_id = interner.intern(sub.blueprint_id);
        nested.embedded = sub.embedded;
        nested.x = sub.x;
        nested.y = sub.y;

        if (sub.embedded && sub.inline_definition) {
            nested.inline_def = std::make_unique<Blueprint>(
                from_flat(*sub.inline_definition, interner));
        }

        bp = bp.with_nested(std::move(nested));
    }

    return bp;
}

} // namespace bp2
```

**Important:** Read the actual `FlatBlueprint` struct in `src/editor/data/flat_blueprint.h` to match field names exactly. The above is a guess -- adjust to match reality.

Build. Run. Pass.

### Step 7.7: BlueprintBridge -- to_flat (new Blueprint to old FlatBlueprint)

**Write test first:**

```cpp
TEST(BlueprintBridge, ToFlatRoundTrip) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    // Build a new Blueprint
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test"));

    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");
    node.x = 100.0f;
    node.y = 200.0f;
    node.params["v_nominal"] = 28.0f;
    bp = bp.with_node(std::move(node));

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("n1")),
        interner.intern("v_out")
    );
    wire.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("n2")),
        interner.intern("in")
    );
    bp = bp.with_wire(std::move(wire));

    // Convert to old FlatBlueprint
    FlatBlueprint flat = bp2::BlueprintBridge::to_flat(bp, interner);

    EXPECT_EQ(flat.id, "test");
    ASSERT_EQ(flat.nodes.size(), 1u);
    EXPECT_EQ(flat.nodes[0].id, "n1");
    EXPECT_EQ(flat.nodes[0].type, "Battery");
    EXPECT_FLOAT_EQ(flat.nodes[0].params.at("v_nominal"), 28.0f);

    ASSERT_EQ(flat.wires.size(), 1u);
    EXPECT_EQ(flat.wires[0].source, "/n1:v_out");
    EXPECT_EQ(flat.wires[0].target, "/n2:in");
}
```

Build. Confirm fail.

**Write production code.** Implement `to_flat`:

```cpp
FlatBlueprint BlueprintBridge::to_flat(Blueprint const& bp,
                                       ui::StringInterner& interner) {
    FlatBlueprint flat;
    flat.id = std::string(interner.resolve(bp.id()));

    PathArena arena(interner);

    for (auto const& node : bp.nodes()) {
        // Map to old NodeInstance type
        NodeInstance fn;
        fn.id = std::string(interner.resolve(node.id));
        fn.type = std::string(interner.resolve(node.type));
        fn.x = node.x;
        fn.y = node.y;
        fn.params = node.params;
        flat.nodes.push_back(std::move(fn));
    }

    for (auto const& wire : bp.wires()) {
        WireConnection fw;
        fw.id = std::string(interner.resolve(wire.id));
        fw.source = arena.to_string(wire.source);
        fw.target = arena.to_string(wire.target);
        fw.domain = wire.domain;
        flat.wires.push_back(std::move(fw));
    }

    for (auto const& nested : bp.nested()) {
        SubBlueprintInstance sub;
        sub.id = std::string(interner.resolve(nested.id));
        sub.blueprint_id = std::string(interner.resolve(nested.blueprint_id));
        sub.embedded = nested.embedded;
        sub.x = nested.x;
        sub.y = nested.y;

        if (nested.embedded && nested.inline_def) {
            sub.inline_definition = std::make_unique<FlatBlueprint>(
                to_flat(*nested.inline_def, interner));
        }

        flat.sub_instances.push_back(std::move(sub));
    }

    return flat;
}
```

Build. Run. Pass.

### Step 7.8: BlueprintBridge -- from_type_def, to_type_def

These convert between `TypeDefinition` (the old blueprint type used by the simulator) and `bp2::Blueprint`. The logic is similar to `from_flat` / `to_flat` but may include additional fields like `interface` ports.

**Write tests** following the same pattern as Steps 7.6-7.7.

**Implement** by reading `src/editor/data/type_def_convert.h` and `src/json_parser/json_parser.h` to understand `TypeDefinition`'s structure.

### Step 7.9: Integration test -- load old blueprint file, convert, flatten

**Write test:**

```cpp
TEST(BlueprintBridge, IntegrationLoadFlatten) {
     ui::StringInterner interner;

     // Load an existing blueprint file from library/
     std::filesystem::path bp_path = "library/GSI.blueprint";
     ASSERT_TRUE(std::filesystem::exists(bp_path));

     // Parse with old parser
     auto flat = parse_flat_blueprint(bp_path);  // Old function

     // Convert to new
     bp2::Blueprint bp = bp2::BlueprintBridge::from_flat(flat, interner);

     // Resolve interfaces from registry
     TypeRegistry reg = load_type_registry("library/");

    // Flatten
    bp2::Flattener flattener(reg);
    bp2::FlatNetlist netlist = flattener.flatten(bp, interner);

    EXPECT_GT(netlist.components.size(), 0u);
}
```

This test validates the full pipeline: old file -> old type -> new type -> flatten.

## Final Verification

```bash
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest --output-on-failure
```

## Files Created This Phase

```
src/blueprint_v2/editor_model/editor_model.h
src/blueprint_v2/editor_model/editor_model.cpp
src/blueprint_v2/bridge/blueprint_bridge.h
src/blueprint_v2/bridge/blueprint_bridge.cpp
tests/blueprint_v2/test_editor_model.cpp
tests/blueprint_v2/test_bridge.cpp
```

## Lines Modified

- `src/blueprint_v2/CMakeLists.txt`: add new .cpp files
- `tests/CMakeLists.txt`: add new test targets

## What Phase 7 Enables

After this phase:
- The editor can use `bp2::EditorModel` with undo/redo
- Old blueprint files can be loaded and converted to the new system
- The new `Flattener` can produce netlists for the simulator
- The bridge layer allows incremental migration without breaking existing code
