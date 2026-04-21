# Phase 3: Blueprint Canonical Type

Historical note: this phase document predates the parser/registry cleanup. References below to legacy `json_parser` paths or APIs are historical and do not describe the current architecture.

## Goal

Create the single canonical `bp2::Blueprint` type. It stores nodes, wires, nested instances, and an interface. Mutations return new values (immutable design). Indices are lazy and derived.

## Files To Create

```
src/blueprint_v2/blueprint/blueprint.h       <- Blueprint class, Node/Wire/Nested structs
src/blueprint_v2/blueprint/blueprint.cpp     <- implementation
tests/blueprint_v2/test_blueprint.cpp        <- all tests for this phase
```

## Prerequisites

- Phase 1 (Path, PathArena) complete
- Phase 2 (PortDescriptor, Interface) complete

## Step-by-Step Instructions

### Step 3.1: CMake setup

1. Add `blueprint/blueprint.cpp` to `src/blueprint_v2/CMakeLists.txt` sources:

```cmake
add_library(blueprint_v2 STATIC
    path/path.cpp
    interface/interface.cpp
    blueprint/blueprint.cpp
)
```

2. Add test target in `tests/CMakeLists.txt`:

```cmake
add_executable(bp2_blueprint_tests
    blueprint_v2/test_blueprint.cpp
)
target_include_directories(bp2_blueprint_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
)
target_link_libraries(bp2_blueprint_tests PRIVATE
    blueprint_v2
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(bp2_blueprint_tests)
```

3. Create empty placeholder files:
   - `src/blueprint_v2/blueprint/blueprint.h`: `#pragma once` + `namespace bp2 {}`
   - `src/blueprint_v2/blueprint/blueprint.cpp`: `#include "blueprint.h"`
   - `tests/blueprint_v2/test_blueprint.cpp`: placeholder test

4. Build. Placeholder passes.

### Step 3.2: Blueprint::Node struct

**Write test first** in `test_blueprint.cpp`:

```cpp
#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/blueprint/blueprint.h"

TEST(BlueprintNode, ConstructAndAccess) {
    ui::StringInterner interner;
    bp2::Blueprint::Node node;
    node.id = interner.intern("bat1");
    node.type = interner.intern("Battery");
    node.x = 100.0f;
    node.y = 200.0f;
    EXPECT_EQ(interner.resolve(node.id), "bat1");
    EXPECT_EQ(interner.resolve(node.type), "Battery");
    EXPECT_FLOAT_EQ(node.x, 100.0f);
}
```

Build. Confirm fail.

**Write production code** in `blueprint.h`:

```cpp
#pragma once
#include "ui/core/interned_id.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/path/path.h"
#include <vector>
#include <unordered_map>
#include <optional>
#include <string>

namespace bp2 {

class Blueprint {
public:
    struct Node {
        ui::InternedId id;
        ui::InternedId type;
        Interface iface;
        std::unordered_map<std::string, float> params;
        float x = 0.0f;
        float y = 0.0f;
    };

    Blueprint() = default;
};

} // namespace bp2
```

Build. Run. Pass.

### Step 3.3: Blueprint::Wire struct

**Write test first:**

```cpp
TEST(BlueprintWire, ConstructAndAccess) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto root = arena.root();
    auto src = arena.make_port(
        arena.make_node(root, interner.intern("b1")),
        interner.intern("v_out")
    );
    auto tgt = arena.make_port(
        arena.make_node(root, interner.intern("r1")),
        interner.intern("in")
    );

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = src;
    wire.target = tgt;
    wire.domain = Domain::Electrical;

    EXPECT_EQ(interner.resolve(wire.id), "w1");
    EXPECT_EQ(wire.source, src);
    EXPECT_EQ(wire.domain, Domain::Electrical);
}
```

Build. Confirm fail.

**Write production code.** Add inside `Blueprint` class in `blueprint.h`:

```cpp
    struct Wire {
        ui::InternedId id;
        Path source;
        Path target;
        Domain domain = Domain::Electrical;

        Wire() : source(PathKind::Root, ui::InternedId{}, 0),
                 target(PathKind::Root, ui::InternedId{}, 0) {}
    };
```

Note: Wire needs a default constructor because `Path` has no public default constructor. Make `Path`'s constructor that takes `(PathKind, InternedId, uint32_t)` public, or add a static `Path::invalid()` factory. Simpler: make the 3-arg constructor public but document it. Alternatively, store paths as `std::optional<Path>` -- no, too heavy. Best approach: add a default constructor to `Path`:

In `path.h`, add to `Path`:

```cpp
public:
    Path() : kind_(PathKind::Root), segment_(), parent_idx_(0) {}
```

Now Wire can have a default constructor that uses default-constructed Paths.

Build. Run. Pass.

### Step 3.4: Blueprint::Nested struct

**Write test first:**

```cpp
TEST(BlueprintNested, ReferenceMode) {
    ui::StringInterner interner;
    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("power_system");
    nested.embedded = false;
    EXPECT_FALSE(nested.embedded);
    EXPECT_FALSE(nested.inline_def.has_value());
}

TEST(BlueprintNested, EmbeddedMode) {
    ui::StringInterner interner;
    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.embedded = true;
    nested.inline_def = bp2::Blueprint();
    EXPECT_TRUE(nested.embedded);
    EXPECT_TRUE(nested.inline_def.has_value());
}
```

Build. Confirm fail.

**Write production code.** Add inside `Blueprint` class:

```cpp
    struct Nested {
        ui::InternedId id;
        ui::InternedId blueprint_id;  // empty if embedded
        bool embedded = false;
        std::optional<Blueprint> inline_def;  // populated if embedded
        Interface iface;
        float x = 0.0f;
        float y = 0.0f;
    };
```

Note: `std::optional<Blueprint>` requires `Blueprint` to be a complete type. Since `Nested` is defined inside `Blueprint`, `Blueprint` is incomplete at that point. Fix: use `std::unique_ptr<Blueprint>` instead.

Change to:

```cpp
    struct Nested {
        ui::InternedId id;
        ui::InternedId blueprint_id;
        bool embedded = false;
        std::unique_ptr<Blueprint> inline_def;
        Interface iface;
        float x = 0.0f;
        float y = 0.0f;
    };
```

Update the test:

```cpp
TEST(BlueprintNested, EmbeddedMode) {
    ui::StringInterner interner;
    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.embedded = true;
    nested.inline_def = std::make_unique<bp2::Blueprint>();
    EXPECT_TRUE(nested.embedded);
    EXPECT_TRUE(nested.inline_def != nullptr);
}
```

Add `#include <memory>` to `blueprint.h`.

Build. Run. Pass.

### Step 3.5: Blueprint -- add/get nodes

**Write test first:**

```cpp
TEST(Blueprint, EmptyByDefault) {
    bp2::Blueprint bp;
    EXPECT_TRUE(bp.nodes().empty());
    EXPECT_TRUE(bp.wires().empty());
    EXPECT_TRUE(bp.nested().empty());
}

TEST(Blueprint, AddNodeAndFind) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Node node;
    node.id = interner.intern("bat1");
    node.type = interner.intern("Battery");

    bp2::Blueprint bp2_val = bp.with_node(std::move(node));
    EXPECT_EQ(bp2_val.nodes().size(), 1u);

    auto* found = bp2_val.find_node(interner.intern("bat1"));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(interner.resolve(found->type), "Battery");
}

TEST(Blueprint, FindNodeNotFound) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    EXPECT_EQ(bp.find_node(interner.intern("nope")), nullptr);
}

TEST(Blueprint, WithNodeDoesNotMutateOriginal) {
    ui::StringInterner interner;
    bp2::Blueprint original;

    bp2::Blueprint::Node node;
    node.id = interner.intern("x");
    node.type = interner.intern("T");

    bp2::Blueprint modified = original.with_node(std::move(node));
    EXPECT_EQ(original.nodes().size(), 0u);
    EXPECT_EQ(modified.nodes().size(), 1u);
}
```

Build. Confirm fail.

**Write production code.** Expand `Blueprint` class in `blueprint.h`:

```cpp
class Blueprint {
public:
    // ... (Node, Wire, Nested structs as above) ...

    Blueprint() = default;

    // --- Accessors ---
    ui::InternedId id() const { return id_; }
    std::string const& display_name() const { return display_name_; }
    Interface const& iface() const { return iface_; }
    std::vector<Node> const& nodes() const { return nodes_; }
    std::vector<Wire> const& wires() const { return wires_; }
    std::vector<Nested> const& nested() const { return nested_; }

    // --- Lookup (O(1) via lazy index) ---
    Node const* find_node(ui::InternedId id) const;
    Wire const* find_wire(ui::InternedId id) const;
    Nested const* find_nested(ui::InternedId id) const;

    // --- Immutable mutations (return new Blueprint) ---
    Blueprint with_node(Node n) const;
    Blueprint without_node(ui::InternedId id) const;
    Blueprint with_wire(Wire w) const;
    Blueprint without_wire(ui::InternedId id) const;
    Blueprint with_nested(Nested n) const;
    Blueprint without_nested(ui::InternedId id) const;
    Blueprint with_id(ui::InternedId id) const;
    Blueprint with_display_name(std::string name) const;
    Blueprint with_interface(Interface iface) const;

private:
    ui::InternedId id_;
    std::string display_name_;
    Interface iface_;
    std::vector<Node> nodes_;
    std::vector<Wire> wires_;
    std::vector<Nested> nested_;

    // Lazy index
    mutable std::unordered_map<ui::InternedId, size_t> node_idx_;
    mutable bool node_idx_valid_ = false;

    void ensure_node_index() const;
};
```

**Write implementation** in `blueprint.cpp`:

```cpp
#include "blueprint.h"

namespace bp2 {

void Blueprint::ensure_node_index() const {
    if (node_idx_valid_) return;
    node_idx_.clear();
    node_idx_.reserve(nodes_.size());
    for (size_t i = 0; i < nodes_.size(); ++i) {
        node_idx_[nodes_[i].id] = i;
    }
    node_idx_valid_ = true;
}

Blueprint::Node const* Blueprint::find_node(ui::InternedId id) const {
    ensure_node_index();
    auto it = node_idx_.find(id);
    if (it == node_idx_.end()) return nullptr;
    return &nodes_[it->second];
}

Blueprint Blueprint::with_node(Node n) const {
    Blueprint copy = *this;
    copy.nodes_.push_back(std::move(n));
    copy.node_idx_valid_ = false;
    return copy;
}

Blueprint Blueprint::without_node(ui::InternedId id) const {
    Blueprint copy = *this;
    copy.nodes_.erase(
        std::remove_if(copy.nodes_.begin(), copy.nodes_.end(),
            [id](Node const& n) { return n.id == id; }),
        copy.nodes_.end()
    );
    copy.node_idx_valid_ = false;
    return copy;
}

} // namespace bp2
```

Add `#include <algorithm>` to `blueprint.cpp`.

Build. Run. Pass.

### Step 3.6: Blueprint -- wire operations

**Write test first:**

```cpp
TEST(Blueprint, AddWireAndFind) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("a")),
        interner.intern("out")
    );
    wire.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("b")),
        interner.intern("in")
    );

    bp2::Blueprint bp2_val = bp.with_wire(std::move(wire));
    EXPECT_EQ(bp2_val.wires().size(), 1u);

    auto* found = bp2_val.find_wire(interner.intern("w1"));
    ASSERT_NE(found, nullptr);
}

TEST(Blueprint, WithoutWire) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    bp = bp.with_wire(std::move(wire));
    EXPECT_EQ(bp.wires().size(), 1u);

    bp = bp.without_wire(interner.intern("w1"));
    EXPECT_EQ(bp.wires().size(), 0u);
}
```

Build. Confirm fail.

**Write production code.** Add to `blueprint.h` private section:

```cpp
    mutable std::unordered_map<ui::InternedId, size_t> wire_idx_;
    mutable bool wire_idx_valid_ = false;
    void ensure_wire_index() const;
```

Add to `blueprint.cpp`:

```cpp
void Blueprint::ensure_wire_index() const {
    if (wire_idx_valid_) return;
    wire_idx_.clear();
    wire_idx_.reserve(wires_.size());
    for (size_t i = 0; i < wires_.size(); ++i) {
        wire_idx_[wires_[i].id] = i;
    }
    wire_idx_valid_ = true;
}

Blueprint::Wire const* Blueprint::find_wire(ui::InternedId id) const {
    ensure_wire_index();
    auto it = wire_idx_.find(id);
    if (it == wire_idx_.end()) return nullptr;
    return &wires_[it->second];
}

Blueprint Blueprint::with_wire(Wire w) const {
    Blueprint copy = *this;
    copy.wires_.push_back(std::move(w));
    copy.wire_idx_valid_ = false;
    return copy;
}

Blueprint Blueprint::without_wire(ui::InternedId id) const {
    Blueprint copy = *this;
    copy.wires_.erase(
        std::remove_if(copy.wires_.begin(), copy.wires_.end(),
            [id](Wire const& w) { return w.id == id; }),
        copy.wires_.end()
    );
    copy.wire_idx_valid_ = false;
    return copy;
}
```

Build. Run. Pass.

### Step 3.7: Blueprint -- nested operations

**Write test first:**

```cpp
TEST(Blueprint, AddNestedAndFind) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("power_system");
    nested.embedded = false;

    bp = bp.with_nested(std::move(nested));
    EXPECT_EQ(bp.nested().size(), 1u);

    auto* found = bp.find_nested(interner.intern("sub1"));
    ASSERT_NE(found, nullptr);
    EXPECT_FALSE(found->embedded);
}

TEST(Blueprint, WithoutNested) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    bp = bp.with_nested(std::move(nested));
    bp = bp.without_nested(interner.intern("sub1"));
    EXPECT_EQ(bp.nested().size(), 0u);
}
```

Build. Confirm fail.

**Write production code.** Follow the exact same pattern as nodes and wires. Add private members:

```cpp
    mutable std::unordered_map<ui::InternedId, size_t> nested_idx_;
    mutable bool nested_idx_valid_ = false;
    void ensure_nested_index() const;
```

Implement `ensure_nested_index`, `find_nested`, `with_nested`, `without_nested` in `blueprint.cpp` following the identical pattern.

**Important:** `Blueprint::Nested` contains `std::unique_ptr<Blueprint>`, so `Blueprint` is not trivially copyable. The copy constructor of `Blueprint` must deep-copy nested blueprints. Add to `blueprint.h`:

```cpp
    Blueprint(Blueprint const& other);
    Blueprint& operator=(Blueprint const& other);
    Blueprint(Blueprint&&) = default;
    Blueprint& operator=(Blueprint&&) = default;
```

Implement in `blueprint.cpp`:

```cpp
Blueprint::Blueprint(Blueprint const& other)
    : id_(other.id_)
    , display_name_(other.display_name_)
    , iface_(other.iface_)
    , nodes_(other.nodes_)
    , wires_(other.wires_)
    , node_idx_valid_(false)
    , wire_idx_valid_(false)
    , nested_idx_valid_(false) {
    nested_.reserve(other.nested_.size());
    for (auto const& n : other.nested_) {
        Nested copy;
        copy.id = n.id;
        copy.blueprint_id = n.blueprint_id;
        copy.embedded = n.embedded;
        copy.iface = n.iface;
        copy.x = n.x;
        copy.y = n.y;
        if (n.inline_def) {
            copy.inline_def = std::make_unique<Blueprint>(*n.inline_def);
        }
        nested_.push_back(std::move(copy));
    }
}

Blueprint& Blueprint::operator=(Blueprint const& other) {
    if (this != &other) {
        Blueprint tmp(other);
        *this = std::move(tmp);
    }
    return *this;
}
```

Build. Run. Pass.

### Step 3.8: Blueprint -- with_id, with_display_name, with_interface

**Write test first:**

```cpp
TEST(Blueprint, WithId) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("my_bp"));
    EXPECT_EQ(interner.resolve(bp.id()), "my_bp");
}

TEST(Blueprint, WithDisplayName) {
    bp2::Blueprint bp;
    bp = bp.with_display_name("Power System");
    EXPECT_EQ(bp.display_name(), "Power System");
}

TEST(Blueprint, WithInterface) {
    ui::StringInterner interner;
    bp2::Interface iface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input}
    });
    bp2::Blueprint bp;
    bp = bp.with_interface(iface);
    EXPECT_EQ(bp.iface().size(), 1u);
}
```

Build. Confirm fail.

**Write production code** in `blueprint.cpp`:

```cpp
Blueprint Blueprint::with_id(ui::InternedId id) const {
    Blueprint copy = *this;
    copy.id_ = id;
    return copy;
}

Blueprint Blueprint::with_display_name(std::string name) const {
    Blueprint copy = *this;
    copy.display_name_ = std::move(name);
    return copy;
}

Blueprint Blueprint::with_interface(Interface iface) const {
    Blueprint copy = *this;
    copy.iface_ = std::move(iface);
    return copy;
}
```

Build. Run. Pass.

### Step 3.9: Blueprint equality

**Write test first:**

```cpp
TEST(Blueprint, EqualEmptyBlueprints) {
    bp2::Blueprint a, b;
    EXPECT_EQ(a, b);
}

TEST(Blueprint, UnequalById) {
    ui::StringInterner interner;
    auto bp_a = bp2::Blueprint().with_id(interner.intern("a"));
    auto bp_b = bp2::Blueprint().with_id(interner.intern("b"));
    EXPECT_NE(bp_a, bp_b);
}

TEST(Blueprint, UnequalByNodes) {
    ui::StringInterner interner;
    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("T");

    auto bp_a = bp2::Blueprint().with_node(node);
    auto bp_b = bp2::Blueprint();
    EXPECT_NE(bp_a, bp_b);
}
```

Build. Confirm fail.

**Write production code.** Add to `Blueprint` in `blueprint.h`:

```cpp
    bool operator==(Blueprint const& other) const;
    bool operator!=(Blueprint const& other) const { return !(*this == other); }
```

Implement in `blueprint.cpp`. Comparing nodes requires `Node::operator==`. Add equality to `Node`:

In `blueprint.h`:
```cpp
    struct Node {
        // ... existing fields ...
        bool operator==(Node const& o) const {
            return id == o.id && type == o.type && params == o.params;
        }
    };
```

Similarly for `Wire`:
```cpp
    struct Wire {
        // ... existing fields ...
        bool operator==(Wire const& o) const {
            return id == o.id && source == o.source
                && target == o.target && domain == o.domain;
        }
    };
```

For `Nested`, equality must deep-compare `inline_def`:
```cpp
    // Add a free function or method -- keep it simple
```

In `blueprint.cpp`:
```cpp
bool Blueprint::operator==(Blueprint const& other) const {
    if (id_ != other.id_) return false;
    if (display_name_ != other.display_name_) return false;
    if (iface_ != other.iface_) return false;
    if (nodes_ != other.nodes_) return false;
    if (wires_ != other.wires_) return false;
    if (nested_.size() != other.nested_.size()) return false;
    for (size_t i = 0; i < nested_.size(); ++i) {
        if (!nested_equals(nested_[i], other.nested_[i])) return false;
    }
    return true;
}
```

Add private static helper:
```cpp
bool Blueprint::nested_equals(Nested const& a, Nested const& b) {
    if (a.id != b.id) return false;
    if (a.blueprint_id != b.blueprint_id) return false;
    if (a.embedded != b.embedded) return false;
    if (a.iface != b.iface) return false;
    bool a_has = (a.inline_def != nullptr);
    bool b_has = (b.inline_def != nullptr);
    if (a_has != b_has) return false;
    if (a_has && *a.inline_def != *b.inline_def) return false;
    return true;
}
```

Declare in header:
```cpp
    static bool nested_equals(Nested const& a, Nested const& b);
```

Build. Run. Pass.

### Step 3.10: Blueprint::clone() and Blueprint::validate()

These are not needed for the core pipeline in Phases 4-6, but will be needed later:

- **`clone(InternedId new_id)`** -- needed in Phase 7 for bake-in. Implementation is trivial: `Blueprint copy = *this; copy = copy.with_id(new_id); return copy;`. Add it when Phase 7 needs it.
- **`validate(TypeRegistry const& registry)`** -- invariant checking (unique IDs, referential integrity, wire validity). Not blocking for the core pipeline. Add when the editor integration (Phase 7) needs pre-commit validation. The method should verify invariants I1-I5 from Part IX of the architecture doc.

These are deferred, not forgotten. If Phase 7 needs them, add them with TDD at that point rather than building them speculatively now.

## Final Verification

```bash
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest --output-on-failure
```

## Files Created This Phase

```
src/blueprint_v2/blueprint/blueprint.h
src/blueprint_v2/blueprint/blueprint.cpp
tests/blueprint_v2/test_blueprint.cpp
```

## Lines Modified

- `src/blueprint_v2/CMakeLists.txt`: add `blueprint/blueprint.cpp`
- `src/blueprint_v2/path/path.h`: add default constructor to `Path`
- `tests/CMakeLists.txt`: add `bp2_blueprint_tests` target
Historical document note: this phase predates the #166 architecture cleanup. References to `src/json_parser/*`, `json_parser`, and `load_type_registry()` are historical and do not describe the current architecture.
