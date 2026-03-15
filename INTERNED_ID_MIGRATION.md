# InternedId Migration Plan

## Overview

We are migrating from string-based IDs (`std::string`) to `ui::InternedId` (a 4-byte `uint32_t` wrapper) for all node/wire/port identifiers in the editor data layer. This eliminates per-frame string hashing and heap allocations.

## Architecture

### Key types (already done)

- **`ui::InternedId`** — defined in `src/ui/core/interned_id.h`. A 4-byte trivially-copyable wrapper around `uint32_t`. Value 0 = empty. Comparisons and hashing are integer-only. Has `==`, `!=`, `<` operators but NO implicit conversion to/from `std::string`.
- **`ui::StringInterner`** — defined in the same file. Maps strings to `InternedId` via `.intern(string_view)` → `InternedId`, and back via `.resolve(InternedId)` → `string_view`.
- **`Blueprint`** — owns a private `ui::StringInterner interner_`. Access via `bp.interner()`.

### What's already migrated (DO NOT TOUCH these files)

These files are fully migrated and compile cleanly:

| File | Status |
|------|--------|
| `src/ui/core/interned_id.h` | Done — InternedId + StringInterner |
| `src/editor/data/port.h` | Done — `EditorPort::name` is `InternedId` |
| `src/editor/data/node.h` | Done — `Node::id` is `InternedId`, `Node::input(InternedId)` / `Node::output(InternedId)` |
| `src/editor/data/wire.h` | Done — `Wire::id`, `WireEnd::node_id`, `WireEnd::port_name` all `InternedId` |
| `src/editor/data/blueprint.h` | Done — owns `StringInterner`, indices use `InternedId` keys |
| `src/editor/data/blueprint.cpp` | Done |
| `src/editor/visual/node/visual_node.h` | Done — constructor takes `(const Node&, const StringInterner&)` |
| `src/editor/visual/node/visual_node.cpp` | Done — resolves `InternedId` to string in constructor |
| `src/editor/visual/node/bus_node_widget.h` | Done — stores `const StringInterner*` |
| `src/editor/visual/node/bus_node_widget.cpp` | Done |
| `src/editor/visual/node/ref_node_widget.h/.cpp` | Done |
| `src/editor/visual/node/text_node_widget.h/.cpp` | Done |
| `src/editor/visual/node/group_node_widget.h/.cpp` | Done |
| `src/editor/visual/node/node_factory.h` | Done — passes `interner` to all widget constructors |
| `src/editor/visual/scene_mutations.h` | Done — uses `InternedId` in signatures |
| `src/editor/visual/scene_mutations.cpp` | Done — resolves `InternedId` to string at widget boundary |
| `src/editor/document.cpp` | Done — compiles cleanly |
| `src/editor/visual/persist.cpp` | Done — compiles cleanly |
| `tests/test_data.cpp` | Done — example of correct pattern using `g_interner.intern("x")` |
| `tests/test_scene_mutations.cpp` | Done — compiles cleanly |
| `tests/test_data_interning.cpp` | Done |

### Visual layer boundary rule

The visual widgets (`NodeWidget`, `BusNodeWidget`, `Wire`, `Port`, etc.) remain **string-based** internally — their `node_id_`, `name_`, port names are all `std::string`. Conversion from `InternedId` to `std::string` happens at the **boundary** in constructors and factory methods using `std::string(interner.resolve(id))`. The Scene's `find()` and `id_index_` use `std::string_view`.

## Files that need fixing

### 1. `src/editor/app.cpp` (16 errors)

**Problem**: `app.cpp` uses `node.id` (now `InternedId`) where it previously was `std::string`. It passes `node.id` to `simulation.get_port_value(string, string)`, assigns string literals to `node.id`, and concatenates `node.id` with strings.

**Errors and fixes:**

#### Lines 14–56: `update_node_content_from_simulation()`
These lines call `simulation.get_port_value(node.id, "v_in")` where `node.id` is now `InternedId`, but `get_port_value` expects `const std::string&`.

**Fix**: Resolve the InternedId to a string before calling:
```cpp
const auto& interner = blueprint.interner();
for (auto& node : blueprint.nodes) {
    std::string node_id_str(interner.resolve(node.id));
    // Then use node_id_str instead of node.id everywhere in this loop
    if (node.type_name == "Voltmeter") {
        float voltage = simulation.get_port_value(node_id_str, "v_in");
        // ...
    }
}
```

#### Line 139: `node.id = unique_id;`
`unique_id` is a `std::string`, but `Node::id` is now `InternedId`.

**Fix**: Intern the string first:
```cpp
node.id = blueprint.interner().intern(unique_id);
```

#### Lines 155–161: `EditorPort` construction from `port_name.c_str()`
`EditorPort` constructor now takes `InternedId` for the name, but we're passing `const char*`.

**Fix**: Intern the port name:
```cpp
auto& interner = blueprint.interner();
// ...
node.inputs.emplace_back(interner.intern(port_name), PortSide::Input, port_def.type);
node.outputs.emplace_back(interner.intern(port_name), PortSide::Output, port_def.type);
```

#### Lines 225–243: `add_blueprint()` — merging sub-blueprint with prefix
These lines do string concatenation on InternedId fields:
```cpp
node.id = unique_id + ":" + node.id;     // ERROR: node.id is InternedId, not string
wire.start.node_id = unique_id + ":" + wire.start.node_id;  // same
```

**Fix**: Resolve the old InternedId, build the new prefixed string, re-intern:
```cpp
auto& interner = blueprint.interner();
for (auto& node : sub_bp.nodes) {
    std::string old_id(sub_bp.interner().resolve(node.id));
    std::string new_id = unique_id + ":" + old_id;
    node.id = interner.intern(new_id);
    node.name = new_id;  // name stays std::string
    // ...
    internal_node_ids.push_back(new_id);
}
```
**Important**: `sub_bp` has its own interner (it came from `expand_type_definition`). You must resolve IDs using `sub_bp.interner()`, then re-intern into `blueprint.interner()` with the prefixed string. Same pattern for wires:
```cpp
for (auto& wire : sub_bp.wires) {
    std::string old_start_node(sub_bp.interner().resolve(wire.start.node_id));
    wire.start.node_id = interner.intern(unique_id + ":" + old_start_node);
    // ... same for wire.end.node_id, wire.id
}
```

#### Lines 243, 258–261: collapsed_node creation
```cpp
collapsed_node.id = unique_id;  // string → InternedId
```
**Fix**: `collapsed_node.id = interner.intern(unique_id);`

And for port creation:
```cpp
collapsed_node.inputs.emplace_back(interner.intern(port_name), PortSide::Input, port.type);
collapsed_node.outputs.emplace_back(interner.intern(port_name), PortSide::Output, port.type);
```

---

### 2. `tests/test_dt_regression.cpp` (19+ errors)

**Problem**: Test helper `make_battery_circuit()` assigns string literals directly to `Node::id`, `WireEnd::node_id`, `WireEnd::port_name`, etc.

**Pattern** (same for all test files):

Old code:
```cpp
Node gnd;
gnd.id = "gnd";           // ERROR: can't assign string to InternedId
gnd.output("v");           // ERROR: Node::output() takes InternedId

Wire w1;
w1.start.node_id = "gnd";     // ERROR
w1.start.port_name = "v";     // ERROR
```

**Fix**: Create a `StringInterner` (or use `bp.interner()`) and intern all strings:
```cpp
static Blueprint make_battery_circuit() {
    Blueprint bp;
    auto& I = bp.interner();  // use the Blueprint's own interner
    bp.grid_step = 16.0f;

    Node gnd;
    gnd.id = I.intern("gnd");
    gnd.name = "Ground";
    gnd.type_name = "RefNode";
    gnd.render_hint = "ref";
    gnd.output(I.intern("v"));
    gnd.at(80, 240);
    gnd.size_wh(40, 40);
    bp.add_node(std::move(gnd));

    // ... same for batt, res ...

    Wire w1;
    w1.id = I.intern("w1");
    w1.start.node_id = I.intern("gnd");
    w1.start.port_name = I.intern("v");
    w1.end.node_id = I.intern("bat");
    w1.end.port_name = I.intern("v_in");
    bp.add_wire(std::move(w1));
    // ...
}
```

**Assertion comparisons**: When tests compare `node.id` against a string, you must intern the string first:
```cpp
// Old: EXPECT_EQ(node.id, "gnd");
// New: EXPECT_EQ(node.id, I.intern("gnd"));
```

Note: `InternedId` does NOT have `operator<<` for gtest output. If a test fails, gtest will complain it can't print the value. To fix this, add an `operator<<` overload. The simplest approach: add this at the top of each test file (or in a shared test header):
```cpp
// Allow gtest to print InternedId values
namespace ui {
inline std::ostream& operator<<(std::ostream& os, InternedId id) {
    return os << "InternedId(" << id.raw() << ")";
}
}
```

---

### 3. `tests/test_bake_in.cpp` (8 errors)

Same pattern as test_dt_regression: assigns string literals to `Node::id`, `WireEnd::node_id`, etc.

**Fix**: Same pattern — use `bp.interner().intern("...")` for all ID assignments.

---

### 4. `tests/test_expand_type_definition.cpp` (8 errors)

**Problem**: Compares `InternedId` against string literals:
```cpp
EXPECT_EQ(bp.nodes[0].id, "bat");   // ERROR: no operator==(InternedId, const char*)
```

**Fix**: Use the blueprint's interner to resolve or intern:
```cpp
auto& I = bp.interner();
EXPECT_EQ(bp.nodes[0].id, I.intern("bat"));
```

Or resolve to string:
```cpp
EXPECT_EQ(I.resolve(bp.nodes[0].id), "bat");
```

---

### 5. `tests/test_sub_blueprint_ref.cpp` (15 errors)

Same pattern: string literal assignments to InternedId fields, string comparisons with InternedId.

**Fix**: Same pattern — intern all string literals.

---

### 6. `tests/test_multi_window.cpp` (19 errors)

Same pattern.

**Fix**: Same pattern — intern all string literals.

---

### 7. `tests/test_document_window_system.cpp` (17 errors)

Same pattern.

**Fix**: Same pattern — intern all string literals.

---

### 8. `tests/test_properties_window.cpp` (11 errors)

**Problem**: Two types of errors:
1. String literal → InternedId assignment (same as above)
2. `NodeFactory::create(node)` called with wrong number of args — now requires `(node, interner)`

**Fix for type 2**: Pass the blueprint's interner:
```cpp
// Old: auto w = NodeFactory::create(node);
// New: auto w = NodeFactory::create(node, bp.interner());
```

---

### 9. `tests/test_params_integrity.cpp` (3 errors)

Same pattern plus gtest `operator<<` issue for printing InternedId in assertion messages.

---

## Mechanical transformation rules (summary)

For each broken file, apply these rules:

| Old code | New code |
|----------|----------|
| `node.id = "foo";` | `node.id = I.intern("foo");` |
| `node.output("v_out");` | `node.output(I.intern("v_out"));` |
| `node.input("v_in");` | `node.input(I.intern("v_in"));` |
| `wire.id = "w1";` | `wire.id = I.intern("w1");` |
| `wire.start.node_id = "bat";` | `wire.start.node_id = I.intern("bat");` |
| `wire.start.port_name = "v";` | `wire.start.port_name = I.intern("v");` |
| `wire.end.node_id = "gnd";` | `wire.end.node_id = I.intern("gnd");` |
| `wire.end.port_name = "v_in";` | `wire.end.port_name = I.intern("v_in");` |
| `WireEnd("node", "port", side)` | `WireEnd(I.intern("node"), I.intern("port"), side)` |
| `wire_output("node", "port")` | `wire_output(I.intern("node"), I.intern("port"))` |
| `wire_input("node", "port")` | `wire_input(I.intern("node"), I.intern("port"))` |
| `EXPECT_EQ(x.id, "foo");` | `EXPECT_EQ(x.id, I.intern("foo"));` |
| `EditorPort("name", side, type)` | `EditorPort(I.intern("name"), side, type)` |
| `NodeFactory::create(node)` | `NodeFactory::create(node, bp.interner())` |
| `simulation.get_port_value(node.id, "port")` | `simulation.get_port_value(std::string(interner.resolve(node.id)), "port")` |
| `"prefix:" + node.id` (string concat) | `"prefix:" + std::string(interner.resolve(node.id))` then re-intern |

**Where does `I` come from?**
- In test files: `auto& I = bp.interner();` at the top of helper functions
- In `app.cpp`: `auto& interner = blueprint.interner();` at the top of the function

## How to get the interner

- **Test functions that create a Blueprint**: Use `bp.interner()` on the Blueprint being constructed
- **Test functions that operate on an existing Blueprint**: Use `bp.interner()`
- **`app.cpp`**: Use `blueprint.interner()` (the `EditorApp::blueprint` member)
- **Helpers using a standalone interner**: Some test files previously used `static ui::StringInterner g_interner;` — this is fine for tests that don't use Blueprint's internal interner

## Operator<< for gtest

Add this to the top of any test file that compares `InternedId` values using `EXPECT_EQ` / `ASSERT_EQ`:

```cpp
#include "ui/core/interned_id.h"

// Allow gtest to print InternedId on assertion failure
namespace ui {
inline std::ostream& operator<<(std::ostream& os, InternedId id) {
    return os << "InternedId(" << id.raw() << ")";
}
}
```

Or better: create `tests/test_helpers.h` with this once and include it everywhere.

## Build & verify

```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | grep "error:"
# Should produce zero errors

cd build && ctest --output-on-failure
# All tests should pass
```

## File fix order (recommended)

1. **`src/editor/app.cpp`** — the only non-test source file. Fix this first to get the editor compiling.
2. **`tests/test_dt_regression.cpp`** — simplest test (small helper function, clear pattern).
3. **`tests/test_bake_in.cpp`**
4. **`tests/test_expand_type_definition.cpp`**
5. **`tests/test_params_integrity.cpp`**
6. **`tests/test_properties_window.cpp`**
7. **`tests/test_sub_blueprint_ref.cpp`**
8. **`tests/test_multi_window.cpp`**
9. **`tests/test_document_window_system.cpp`**
