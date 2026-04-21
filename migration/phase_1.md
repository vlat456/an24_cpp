# Phase 1: Path and PathArena

Historical note: this phase document predates the parser/registry cleanup. References below to legacy `json_parser` paths or APIs are historical and do not describe the current architecture.

## Goal

Create a typed hierarchical path system that replaces the current `"parent:child"` string concatenation. A `Path` is 12 bytes (kind + segment + parent_index), comparison is O(1), and it is impossible to construct an ambiguous or malformed path.

## Files To Create

```
src/blueprint_v2/CMakeLists.txt          <- library target (start here, grows each phase)
src/blueprint_v2/path/path.h             <- Path class, PathArena class
src/blueprint_v2/path/path.cpp           <- Path::to_string(), Path::parse()
tests/blueprint_v2/test_path.cpp         <- all tests for this phase
```

## Prerequisites

- Phase 0 completed (directory structure exists)
- `src/ui/core/interned_id.h` is readable (we use `ui::InternedId`)

## Step-by-Step Instructions

### Step 1.1: Create the CMake infrastructure

1. Create file `src/blueprint_v2/CMakeLists.txt` with content:

```cmake
add_library(blueprint_v2 STATIC
    path/path.cpp
)
target_include_directories(blueprint_v2 PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(blueprint_v2 PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
```

2. Open `src/CMakeLists.txt`. Find the line `add_subdirectory(json_parser)`. Add below it:

```cmake
add_subdirectory(blueprint_v2)
```

3. Create empty file `src/blueprint_v2/path/path.cpp` with just:

```cpp
#include "path.h"
```

4. Create empty file `src/blueprint_v2/path/path.h` with just:

```cpp
#pragma once
namespace bp2 {}
```

5. Add test infrastructure. Open `tests/CMakeLists.txt`. At the end of the file, add:

```cmake
# ==============================================================================
# Blueprint V2 tests
# ==============================================================================

add_executable(bp2_path_tests
    blueprint_v2/test_path.cpp
)
target_include_directories(bp2_path_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(bp2_path_tests PRIVATE
    blueprint_v2
    GTest::gtest_main
)
gtest_discover_tests(bp2_path_tests)
```

6. Create directory `tests/blueprint_v2/` and file `tests/blueprint_v2/test_path.cpp` with:

```cpp
#include <gtest/gtest.h>
#include "blueprint_v2/path/path.h"

TEST(PathArena, Placeholder) {
    EXPECT_TRUE(true);
}
```

7. Build and run:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest -R "bp2_" --output-on-failure
```

Verify: placeholder test passes. All existing tests still pass (`ctest --output-on-failure`).

### Step 1.2: PathArena -- intern root path

**Write test first** in `tests/blueprint_v2/test_path.cpp`:

```cpp
#include "ui/core/interned_id.h"

TEST(PathArena, RootPathHasKindRoot) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path root = arena.root();
    EXPECT_EQ(root.kind(), bp2::PathKind::Root);
}
```

Build. Confirm it fails to compile (no `PathArena`, no `PathKind`).

**Write production code** in `src/blueprint_v2/path/path.h`:

```cpp
#pragma once
#include "ui/core/interned_id.h"
#include <cstdint>
#include <vector>

namespace bp2 {

enum class PathKind : uint8_t {
    Root,
    Node,
    Port,
    Nested,
    Wire
};

class PathArena;  // forward

class Path {
public:
    PathKind kind() const { return kind_; }
    ui::InternedId segment() const { return segment_; }
    uint32_t parent_index() const { return parent_idx_; }

    bool operator==(Path other) const {
        return kind_ == other.kind_
            && segment_ == other.segment_
            && parent_idx_ == other.parent_idx_;
    }
    bool operator!=(Path other) const { return !(*this == other); }

private:
    friend class PathArena;
    Path(PathKind k, ui::InternedId seg, uint32_t parent)
        : kind_(k), segment_(seg), parent_idx_(parent) {}

    PathKind kind_;
    ui::InternedId segment_;
    uint32_t parent_idx_;
};

class PathArena {
public:
    explicit PathArena(ui::StringInterner& interner)
        : interner_(interner) {
        // Index 0 is always the root path
        paths_.emplace_back(PathKind::Root, ui::InternedId{}, 0);
    }

    Path root() const { return paths_[0]; }

private:
    ui::StringInterner& interner_;
    std::vector<Path> paths_;
};

} // namespace bp2
```

Build. Run test. Confirm it passes (GREEN).

### Step 1.3: PathArena -- make_node

**Write test first:**

```cpp
TEST(PathArena, MakeNodeReturnsNodeKind) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path root = arena.root();
    bp2::Path node = arena.make_node(root, interner.intern("battery1"));
    EXPECT_EQ(node.kind(), bp2::PathKind::Node);
    EXPECT_EQ(interner.resolve(node.segment()), "battery1");
}
```

Build. Confirm compile fails (no `make_node`).

**Write production code.** Add to `PathArena` in `path.h`:

```cpp
    Path make_node(Path parent, ui::InternedId node_id) {
        uint32_t parent_idx = index_of(parent);
        uint32_t idx = static_cast<uint32_t>(paths_.size());
        paths_.emplace_back(PathKind::Node, node_id, parent_idx);
        return paths_[idx];
    }
```

Also add the private helper:

```cpp
    uint32_t index_of(Path p) const {
        // Linear scan is fine for now -- paths are small
        for (uint32_t i = 0; i < paths_.size(); ++i) {
            if (paths_[i] == p) return i;
        }
        // Should never happen if paths come from this arena
        return 0;
    }
```

Build. Run test. Confirm pass.

### Step 1.4: make_port, make_nested, make_wire

**Write tests first** (all three in one go since they follow the same pattern):

```cpp
TEST(PathArena, MakePortReturnsPortKind) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path root = arena.root();
    bp2::Path node = arena.make_node(root, interner.intern("bat1"));
    bp2::Path port = arena.make_port(node, interner.intern("v_out"));
    EXPECT_EQ(port.kind(), bp2::PathKind::Port);
    EXPECT_EQ(interner.resolve(port.segment()), "v_out");
}

TEST(PathArena, MakeNestedReturnsNestedKind) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path nested = arena.make_nested(arena.root(), interner.intern("sub1"));
    EXPECT_EQ(nested.kind(), bp2::PathKind::Nested);
}

TEST(PathArena, MakeWireReturnsWireKind) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path wire = arena.make_wire(arena.root(), interner.intern("w1"));
    EXPECT_EQ(wire.kind(), bp2::PathKind::Wire);
}
```

Build. Confirm compile fails.

**Write production code.** Add to `PathArena`:

```cpp
    Path make_port(Path parent, ui::InternedId port_name) {
        uint32_t parent_idx = index_of(parent);
        uint32_t idx = static_cast<uint32_t>(paths_.size());
        paths_.emplace_back(PathKind::Port, port_name, parent_idx);
        return paths_[idx];
    }

    Path make_nested(Path parent, ui::InternedId instance_id) {
        uint32_t parent_idx = index_of(parent);
        uint32_t idx = static_cast<uint32_t>(paths_.size());
        paths_.emplace_back(PathKind::Nested, instance_id, parent_idx);
        return paths_[idx];
    }

    Path make_wire(Path parent, ui::InternedId wire_id) {
        uint32_t parent_idx = index_of(parent);
        uint32_t idx = static_cast<uint32_t>(paths_.size());
        paths_.emplace_back(PathKind::Wire, wire_id, parent_idx);
        return paths_[idx];
    }
```

Build. Run. All pass.

### Step 1.5: Path::parent()

**Write test first:**

```cpp
TEST(PathArena, ParentOfNodeIsRoot) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path root = arena.root();
    bp2::Path node = arena.make_node(root, interner.intern("r1"));
    EXPECT_EQ(arena.parent(node), root);
}

TEST(PathArena, ParentOfPortIsNode) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path node = arena.make_node(arena.root(), interner.intern("r1"));
    bp2::Path port = arena.make_port(node, interner.intern("in"));
    EXPECT_EQ(arena.parent(port), node);
}

TEST(PathArena, ParentOfRootIsRoot) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    EXPECT_EQ(arena.parent(arena.root()), arena.root());
}
```

Build. Confirm compile fails (no `arena.parent()`).

**Write production code.** Add to `PathArena`:

```cpp
    Path parent(Path p) const {
        return paths_[p.parent_index()];
    }
```

Build. Run. All pass.

### Step 1.6: Path::to_string()

**Write test first:**

```cpp
TEST(PathToString, RootIsSlash) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    EXPECT_EQ(arena.to_string(arena.root()), "/");
}

TEST(PathToString, NodePath) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path node = arena.make_node(arena.root(), interner.intern("bat1"));
    EXPECT_EQ(arena.to_string(node), "/bat1");
}

TEST(PathToString, PortPath) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path node = arena.make_node(arena.root(), interner.intern("bat1"));
    bp2::Path port = arena.make_port(node, interner.intern("v_out"));
    EXPECT_EQ(arena.to_string(port), "/bat1:v_out");
}

TEST(PathToString, NestedNodePort) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path sub = arena.make_nested(arena.root(), interner.intern("sub1"));
    bp2::Path node = arena.make_node(sub, interner.intern("r1"));
    bp2::Path port = arena.make_port(node, interner.intern("in"));
    EXPECT_EQ(arena.to_string(port), "/sub1/r1:in");
}

TEST(PathToString, DeepNesting) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path s1 = arena.make_nested(arena.root(), interner.intern("a"));
    bp2::Path s2 = arena.make_nested(s1, interner.intern("b"));
    bp2::Path node = arena.make_node(s2, interner.intern("c"));
    EXPECT_EQ(arena.to_string(node), "/a/b/c");
}
```

Build. Confirm compile fails (no `arena.to_string()`).

**Write production code** in `src/blueprint_v2/path/path.cpp`:

```cpp
#include "path.h"
#include <string>

namespace bp2 {

std::string PathArena::to_string(Path p) const {
    if (p.kind() == PathKind::Root) return "/";

    // Build segments from leaf to root
    std::vector<std::string> segments;
    build_segments(p, segments);

    std::string result;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        result += *it;
    }
    return result;
}

void PathArena::build_segments(Path p, std::vector<std::string>& out) const {
    if (p.kind() == PathKind::Root) return;

    Path par = parent(p);

    if (p.kind() == PathKind::Port) {
        // Ports use ":" separator, attached to parent node
        std::string seg = ":" + std::string(interner_.resolve(p.segment()));
        out.push_back(seg);
    } else {
        // Nodes, Nested, Wire use "/" separator
        std::string seg = "/" + std::string(interner_.resolve(p.segment()));
        out.push_back(seg);
    }

    build_segments(par, out);
}

} // namespace bp2
```

Add declarations to `path.h` in the `PathArena` class:

```cpp
    std::string to_string(Path p) const;

private:
    void build_segments(Path p, std::vector<std::string>& out) const;
```

Add `#include <string>` and `#include <vector>` to `path.h` if not already present.

Build. Run. All pass.

### Step 1.7: PathArena::parse()

**Write test first:**

```cpp
TEST(PathParse, ParseRoot) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto result = arena.parse("/");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind(), bp2::PathKind::Root);
}

TEST(PathParse, ParseNode) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto result = arena.parse("/battery1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind(), bp2::PathKind::Node);
    EXPECT_EQ(interner.resolve(result->segment()), "battery1");
}

TEST(PathParse, ParsePort) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto result = arena.parse("/bat1:v_out");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind(), bp2::PathKind::Port);
    EXPECT_EQ(interner.resolve(result->segment()), "v_out");
}

TEST(PathParse, ParseNestedNodePort) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto result = arena.parse("/sub1/r1:in");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind(), bp2::PathKind::Port);
    EXPECT_EQ(interner.resolve(result->segment()), "in");
}

TEST(PathParse, RoundTrip) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    std::string original = "/a/b/c:port";
    auto parsed = arena.parse(original);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(arena.to_string(*parsed), original);
}

TEST(PathParse, EmptyStringFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto result = arena.parse("");
    EXPECT_FALSE(result.has_value());
}

TEST(PathParse, NoLeadingSlashFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto result = arena.parse("bat1:v_out");
    EXPECT_FALSE(result.has_value());
}
```

Build. Confirm compile fails.

**Write production code** in `path.cpp`:

```cpp
std::optional<Path> PathArena::parse(std::string_view s) {
    if (s.empty() || s[0] != '/') return std::nullopt;
    if (s == "/") return root();

    // Remove leading '/'
    s.remove_prefix(1);

    Path current = root();
    while (!s.empty()) {
        // Find next '/' or end
        auto slash_pos = s.find('/');
        std::string_view token = s.substr(0, slash_pos);

        current = parse_token(token, current);

        if (slash_pos == std::string_view::npos) break;
        s.remove_prefix(slash_pos + 1);
    }

    return current;
}

Path PathArena::parse_token(std::string_view token, Path parent) {
    // Check for ':' (port separator)
    auto colon_pos = token.find(':');
    if (colon_pos != std::string_view::npos) {
        // "node_id:port_name"
        std::string_view node_part = token.substr(0, colon_pos);
        std::string_view port_part = token.substr(colon_pos + 1);

        if (!node_part.empty()) {
            parent = make_node(parent, interner_.intern(node_part));
        }
        return make_port(parent, interner_.intern(port_part));
    }

    // No colon -- it's a node or nested. We use Node by default.
    // The caller (blueprint loading) can reinterpret as Nested if needed.
    return make_node(parent, interner_.intern(token));
}
```

Add declarations to `path.h`:

```cpp
    std::optional<Path> parse(std::string_view s);

private:
    Path parse_token(std::string_view token, Path parent);
```

Add `#include <optional>` and `#include <string_view>` to `path.h`.

Build. Run. All pass.

**Note on parse ambiguity:** `parse()` creates `PathKind::Node` for non-port segments. It cannot distinguish Node from Nested without schema context. This is intentional -- the parse function is for wire endpoint paths where the distinction is made by the blueprint's context. Document this in a comment.

### Step 1.8: Path equality and hash

**Write test first:**

```cpp
TEST(PathEquality, SamePathsAreEqual) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path a = arena.make_node(arena.root(), interner.intern("x"));
    bp2::Path b = arena.make_node(arena.root(), interner.intern("x"));
    EXPECT_EQ(a, b);
}

TEST(PathEquality, DifferentPathsAreNotEqual) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path a = arena.make_node(arena.root(), interner.intern("x"));
    bp2::Path b = arena.make_node(arena.root(), interner.intern("y"));
    EXPECT_NE(a, b);
}

TEST(PathHash, SamePathsSameHash) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Path a = arena.make_node(arena.root(), interner.intern("x"));
    bp2::Path b = arena.make_node(arena.root(), interner.intern("x"));
    std::hash<bp2::Path> h;
    EXPECT_EQ(h(a), h(b));
}
```

Build. Confirm compile fails (no `std::hash<bp2::Path>`).

**Write production code.** Add after the `namespace bp2` closing brace in `path.h`:

```cpp
template <>
struct std::hash<bp2::Path> {
    size_t operator()(bp2::Path p) const noexcept {
        size_t h = std::hash<uint8_t>{}(static_cast<uint8_t>(p.kind()));
        h ^= std::hash<ui::InternedId>{}(p.segment()) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(p.parent_index()) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
```

Build. Run. All pass.

## Final Verification

```bash
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest --output-on-failure
```

All old tests pass. All new `bp2_path_tests` pass.

## Files Created This Phase

```
src/blueprint_v2/CMakeLists.txt
src/blueprint_v2/path/path.h
src/blueprint_v2/path/path.cpp
tests/blueprint_v2/test_path.cpp
```

## Lines Added to Existing Files

- `src/CMakeLists.txt`: 1 line (`add_subdirectory(blueprint_v2)`)
- `tests/CMakeLists.txt`: ~12 lines (test target for `bp2_path_tests`)
