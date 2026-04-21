# Phase 6: Flattener (Hierarchy -> Flat Netlist)

Historical note: this phase description references legacy bp2 registry APIs that were removed. The current canonical registry model lives in `core/model/component_registry.h`, and the current JSON loader lives in `io/json/component_registry_json_loader.h`.

## Goal

Create a pure-function `Flattener` that takes a `bp2::Blueprint` tree and produces a `FlatNetlist`: a flat list of components with signal indices, ready for the JIT solver or AOT codegen. This replaces the old `to_simulator_json()` + `parse_json()` pipeline and the `expand_sub_blueprint_references()` in `json_parser.h`.

Key properties:
- **Pure function**: `Blueprint` + `TypeRegistry` in, `FlatNetlist` out. No side effects.
- **Deterministic**: Same input always produces same output.
- **Hierarchical resolution**: Nested blueprint interfaces are respected; signals merge at boundary ports.
- **No string-based scoping**: All paths are typed `bp2::Path`.

## Files To Create

```
src/blueprint_v2/flattener/flat_netlist.h     <- FlatNetlist, Component, Signal structs
src/blueprint_v2/flattener/flattener.h        <- Flattener class
src/blueprint_v2/flattener/flattener.cpp      <- implementation
tests/blueprint_v2/test_flattener.cpp         <- all tests for this phase
```

## Prerequisites

- Phase 1 complete (Path, PathArena)
- Phase 2 complete (Interface, PortDescriptor)
- Phase 3 complete (Blueprint)
- Phase 4 complete (TypeRegistry in json_parser)

## Step-by-Step Instructions

### Step 6.1: CMake setup

1. Add files to `src/blueprint_v2/CMakeLists.txt`:

```cmake
add_library(blueprint_v2 STATIC
    path/path.cpp
    interface/interface.cpp
    blueprint/blueprint.cpp
    registry/type_registry.cpp
    codec/blueprint_codec.cpp
    flattener/flattener.cpp
)
```

2. Add test target in `tests/CMakeLists.txt`:

```cmake
add_executable(bp2_flattener_tests
    blueprint_v2/test_flattener.cpp
)
target_include_directories(bp2_flattener_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(bp2_flattener_tests PRIVATE
    blueprint_v2
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(bp2_flattener_tests)
```

3. Create placeholder files:
   - `src/blueprint_v2/flattener/flat_netlist.h`: `#pragma once` + `namespace bp2 {}`
   - `src/blueprint_v2/flattener/flattener.h`: `#pragma once` + `namespace bp2 {}`
   - `src/blueprint_v2/flattener/flattener.cpp`: `#include "flattener.h"`
   - `tests/blueprint_v2/test_flattener.cpp`: placeholder test

4. Build. Placeholder passes.

### Step 6.2: FlatNetlist data structures

**Write test first** in `test_flattener.cpp`:

```cpp
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "json_parser/json_parser.h"

TEST(Flattener, SingleNodeNoWires) {
     ui::StringInterner interner;
     auto reg = load_type_registry("library/");

     bp2::Blueprint bp;
    bp2::Blueprint::Node node;
    node.id = interner.intern("bat1");
    node.type = interner.intern("Battery");
    node.iface = reg.interface_of(interner.intern("Battery"));
    bp = bp.with_node(std::move(node));

    bp2::Flattener flattener(reg);
    bp2::FlatNetlist netlist = flattener.flatten(bp, interner);

    EXPECT_EQ(netlist.components.size(), 1u);
    EXPECT_EQ(interner.resolve(netlist.components[0].type), "Battery");
    // Each port gets its own signal (no wires to merge them)
    EXPECT_EQ(netlist.components[0].port_signals.size(), 2u);
    EXPECT_GE(netlist.signal_count, 2u);
}
```

Build. Confirm fail (no `FlatNetlist`).

**Write production code** in `flat_netlist.h`:

```cpp
#pragma once
#include "ui/core/interned_id.h"
#include "blueprint_v2/path/path.h"
#include "json_parser.h"  // for Domain
#include <vector>
#include <unordered_map>
#include <string>

namespace bp2 {

using SignalIndex = uint32_t;

struct FlatNetlist {
    struct Component {
        Path path;                   // Hierarchical path (e.g. /sub1/bat1)
        ui::InternedId type;         // Component type name
        std::unordered_map<std::string, float> params;
        std::vector<std::pair<ui::InternedId, SignalIndex>> port_signals;
    };

    struct Signal {
        SignalIndex index;
        Domain domain;
        std::vector<Path> connected_ports;  // All port paths on this signal
    };

    std::vector<Component> components;
    std::vector<Signal> signals;
    uint32_t signal_count = 0;
};

} // namespace bp2
```

Build. Run. Pass.

### Step 6.3: Flatten single node, no wires

**Write test first:**

```cpp
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "json_parser/json_parser.h"

TEST(Flattener, SingleNodeNoWires) {
     ui::StringInterner interner;
     auto reg = load_type_registry("library/");

     bp2::Blueprint bp;
     bp2::Blueprint::Node node;
     node.id = interner.intern("bat1");
     node.type = interner.intern("Battery");
     node.iface = reg.interface_of(interner.intern("Battery"));
     bp = bp.with_node(std::move(node));

     bp2::Flattener flattener(reg);
     bp2::FlatNetlist netlist = flattener.flatten(bp, interner);

     EXPECT_EQ(netlist.components.size(), 1u);
     EXPECT_EQ(interner.resolve(netlist.components[0].type), "Battery");
     // Each port gets its own signal (no wires to merge them)
     EXPECT_EQ(netlist.components[0].port_signals.size(), 2u);
     EXPECT_GE(netlist.signal_count, 2u);
}
```

Build. Confirm fail (no `Flattener` class).

**Write production code** in `flattener.h`:

```cpp
#pragma once
#include "flat_netlist.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "json_parser/json_parser.h"
#include "blueprint_v2/path/path.h"

namespace bp2 {

class Flattener {
public:
     explicit Flattener(TypeRegistry const& registry);

    FlatNetlist flatten(Blueprint const& root,
                        ui::StringInterner& interner);

private:
     TypeRegistry const& registry_;
    std::optional<PathArena> arena_;

    void visit_blueprint(
        Blueprint const& bp,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& boundary_signals,
        FlatNetlist& out,
        ui::StringInterner& interner);

    void emit_component(
        Blueprint::Node const& node,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out,
        ui::StringInterner& interner);

    SignalIndex get_or_create_signal(
        Path port_path,
        Domain domain,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out);

    void process_wires(
        Blueprint const& bp,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out,
        ui::StringInterner& interner);

    void merge_signals(
        SignalIndex a,
        SignalIndex b,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out);
};

} // namespace bp2
```

Implement in `flattener.cpp`:

```cpp
#include "flattener.h"
#include <stdexcept>

namespace bp2 {

Flattener::Flattener(TypeRegistry const& registry)
     : registry_(registry) {}

FlatNetlist Flattener::flatten(Blueprint const& root,
                                ui::StringInterner& interner) {
    arena_.emplace(interner);
    FlatNetlist out;
    std::unordered_map<Path, SignalIndex> signals;

    process_wires(root, arena_->root(), signals, out, interner);
    visit_blueprint(root, arena_->root(), signals, out, interner);

    return out;
}
```

**Note on construction:** The `Flattener` constructor above has an issue -- storing a reference to `PathArena` before `flatten()` provides the interner. Fix: have `PathArena` be constructed inside `flatten()` and stored via `std::optional<PathArena>`:

Change private member from `PathArena arena_;` to `std::optional<PathArena> arena_;` and in `flatten()`:

```cpp
arena_.emplace(interner);
```

Access via `*arena_` everywhere else.

Build. Run. Pass.

### Step 6.4: Flatten two nodes with one wire

**Write test first:**

```cpp
TEST(Flattener, TwoNodesOneWire) {
     ui::StringInterner interner;
     auto reg = load_type_registry("library/");
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;

    // Battery node
    bp2::Blueprint::Node bat;
    bat.id = interner.intern("bat1");
    bat.type = interner.intern("Battery");
    bat.iface = reg.interface_of(interner.intern("Battery"));
    bp = bp.with_node(std::move(bat));

    // Resistor node
    bp2::Blueprint::Node res;
    res.id = interner.intern("r1");
    res.type = interner.intern("Resistor");
    res.iface = reg.interface_of(interner.intern("Resistor"));
    bp = bp.with_node(std::move(res));

    // Wire: bat1:v_out -> r1:in
    bp2::Blueprint::Wire w;
    w.id = interner.intern("w1");
    w.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("bat1")),
        interner.intern("v_out")
    );
    w.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("r1")),
        interner.intern("in")
    );
    w.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w));

    bp2::Flattener flattener(reg);
    bp2::FlatNetlist netlist = flattener.flatten(bp, interner);

    EXPECT_EQ(netlist.components.size(), 2u);

    // bat1:v_out and r1:in should share the same signal
    SignalIndex bat_vout = 0xFFFFFFFF;
    SignalIndex r1_in = 0xFFFFFFFF;
    for (auto const& comp : netlist.components) {
        for (auto const& [port_name, sig] : comp.port_signals) {
            if (interner.resolve(port_name) == "v_out" &&
                interner.resolve(comp.type) == "Battery") {
                bat_vout = sig;
            }
            if (interner.resolve(port_name) == "in" &&
                interner.resolve(comp.type) == "Resistor") {
                r1_in = sig;
            }
        }
    }
    EXPECT_EQ(bat_vout, r1_in);
}
```

Build. Run. Pass (the wire processing in Step 6.3 merges the signals).

### Step 6.5: Flatten with multiple wires, verify signal count

**Write test first:**

```cpp
TEST(Flattener, ThreeNodesChainedSignalCount) {
     ui::StringInterner interner;
     auto reg = load_type_registry("library/");
    bp2::PathArena arena(interner);

    // Battery -> Resistor -> LED (chain)
    bp2::Blueprint bp;

    bp2::Blueprint::Node bat;
    bat.id = interner.intern("bat1");
    bat.type = interner.intern("Battery");
    bat.iface = reg.interface_of(interner.intern("Battery"));
    bp = bp.with_node(std::move(bat));

    bp2::Blueprint::Node res;
    res.id = interner.intern("r1");
    res.type = interner.intern("Resistor");
    res.iface = reg.interface_of(interner.intern("Resistor"));
    bp = bp.with_node(std::move(res));

    bp2::Blueprint::Node led;
    led.id = interner.intern("led1");
    led.type = interner.intern("LED");
    led.iface = reg.interface_of(interner.intern("LED"));
    bp = bp.with_node(std::move(led));

    // Wire 1: bat1:v_out -> r1:in
    bp2::Blueprint::Wire w1;
    w1.id = interner.intern("w1");
    w1.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("bat1")),
        interner.intern("v_out"));
    w1.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("r1")),
        interner.intern("in"));
    w1.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w1));

    // Wire 2: r1:out -> led1:v_in
    bp2::Blueprint::Wire w2;
    w2.id = interner.intern("w2");
    w2.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("r1")),
        interner.intern("out"));
    w2.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("led1")),
        interner.intern("v_in"));
    w2.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w2));

    bp2::Flattener flattener(reg);
    bp2::FlatNetlist netlist = flattener.flatten(bp, interner);

    EXPECT_EQ(netlist.components.size(), 3u);
    // bat1:v_in, bat1:v_out=r1:in, r1:out=led1:v_in, led1:ground
    // That's 4 distinct signals (bat_vin, shared1, shared2, led_gnd)
    // Count unique signal indices actually used by components
    std::set<bp2::SignalIndex> unique_sigs;
    for (auto const& comp : netlist.components) {
        for (auto const& [_, sig] : comp.port_signals) {
            unique_sigs.insert(sig);
        }
    }
    // 2 merged pairs + bat1:v_in + led1:ground = 4 unique signals
    EXPECT_EQ(unique_sigs.size(), 4u);
}
```

Build. Run. Pass.

### Step 6.6: Flatten nested blueprint (one level)

**Write test first:**

```cpp
TEST(Flattener, NestedBlueprintExpands) {
     ui::StringInterner interner;
     bp2::PathArena arena(interner);

     // Create a sub-blueprint with 1 resistor, interface: in, out
     bp2::Blueprint inner;
     inner = inner.with_id(interner.intern("sub_type"));
     inner = inner.with_interface(bp2::Interface({
         {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
         {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
     }));

     bp2::Blueprint::Node r1;
     r1.id = interner.intern("r1");
     r1.type = interner.intern("Resistor");
     r1.iface = bp2::Interface({
         {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
         {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
     });
     inner = inner.with_node(std::move(r1));

     // Wires inside sub-blueprint: interface:in -> r1:in, r1:out -> interface:out
     bp2::PathArena inner_arena(interner);
     bp2::Blueprint::Wire iw1;
     iw1.id = interner.intern("iw1");
     iw1.source = inner_arena.make_port(inner_arena.root(), interner.intern("in"));
     iw1.target = inner_arena.make_port(
         inner_arena.make_node(inner_arena.root(), interner.intern("r1")),
         interner.intern("in"));
     iw1.domain = Domain::Electrical;
     inner = inner.with_wire(std::move(iw1));

     bp2::Blueprint::Wire iw2;
     iw2.id = interner.intern("iw2");
     iw2.source = inner_arena.make_port(
         inner_arena.make_node(inner_arena.root(), interner.intern("r1")),
         interner.intern("out"));
     iw2.target = inner_arena.make_port(inner_arena.root(), interner.intern("out"));
     iw2.domain = Domain::Electrical;
     inner = inner.with_wire(std::move(iw2));

     // Register it
     TypeRegistry reg = load_type_registry("library/");
    reg.register_blueprint(interner.intern("sub_type"), inner.iface());

    // Root blueprint: Battery -> sub1:in, sub1:out -> LED
    bp2::Blueprint root;
    bp2::Blueprint::Node bat;
    bat.id = interner.intern("bat1");
    bat.type = interner.intern("Battery");
    bat.iface = reg.interface_of(interner.intern("Battery"));
    root = root.with_node(std::move(bat));

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.embedded = true;
    nested.inline_def = std::make_unique<bp2::Blueprint>(inner);
    nested.iface = inner.iface();
    root = root.with_nested(std::move(nested));

    bp2::Blueprint::Node led;
    led.id = interner.intern("led1");
    led.type = interner.intern("LED");
    led.iface = reg.interface_of(interner.intern("LED"));
    root = root.with_node(std::move(led));

    // Wire: bat1:v_out -> sub1:in
    bp2::Blueprint::Wire w1;
    w1.id = interner.intern("w1");
    w1.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("bat1")),
        interner.intern("v_out"));
    w1.target = arena.make_port(
        arena.make_nested(arena.root(), interner.intern("sub1")),
        interner.intern("in"));
    w1.domain = Domain::Electrical;
    root = root.with_wire(std::move(w1));

    // Wire: sub1:out -> led1:v_in
    bp2::Blueprint::Wire w2;
    w2.id = interner.intern("w2");
    w2.source = arena.make_port(
        arena.make_nested(arena.root(), interner.intern("sub1")),
        interner.intern("out"));
    w2.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("led1")),
        interner.intern("v_in"));
    w2.domain = Domain::Electrical;
    root = root.with_wire(std::move(w2));

    // Flatten
    bp2::Flattener flattener(reg);
    bp2::FlatNetlist netlist = flattener.flatten(root, interner);

    // Expect 3 components: bat1, sub1/r1, led1 (sub1 itself is expanded, not emitted)
    EXPECT_EQ(netlist.components.size(), 3u);

    // Check bat1:v_out and sub1/r1:in share a signal
    // Check sub1/r1:out and led1:v_in share a signal
    // (Verify signal sharing by inspecting port_signals)
}
```

Build. Confirm fail (visit_blueprint doesn't recurse into nested yet).

**Write production code.** Add nested handling to `visit_blueprint` in `flattener.cpp`:

```cpp
void Flattener::visit_blueprint(
    Blueprint const& bp,
    Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    FlatNetlist& out,
    ui::StringInterner& interner) {

    for (auto const& node : bp.nodes()) {
        emit_component(node, prefix, signals, out, interner);
    }

    for (auto const& nested : bp.nested()) {
        visit_nested(nested, prefix, signals, out, interner);
    }
}
```

Add new private method `visit_nested`:

```cpp
void Flattener::visit_nested(
    Blueprint::Nested const& nested,
    Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    FlatNetlist& out,
    ui::StringInterner& interner) {

    Path nested_path = arena_->make_nested(prefix, nested.id);

    // Resolve the blueprint to visit
    Blueprint const* bp_to_visit = nullptr;
    if (nested.embedded && nested.inline_def) {
        bp_to_visit = nested.inline_def.get();
    } else {
        // Reference mode: look up in registry
        auto* entry = registry_.find(nested.blueprint_id);
        if (entry && entry->blueprint) {
            bp_to_visit = entry->blueprint;
        } else {
            return;  // Unknown blueprint type, skip
        }
    }

    // Build boundary signal mapping:
    // Map interface port paths on the nested instance to the
    // corresponding internal root-level interface port paths
    std::unordered_map<Path, SignalIndex> nested_signals;
    for (auto const& iface_port : nested.iface) {
        Path outer_port = arena_->make_port(nested_path, iface_port.name);
        auto it = signals.find(outer_port);
        if (it != signals.end()) {
            // The inner blueprint's interface port: /:port_name
            Path inner_port = arena_->make_port(arena_->root(), iface_port.name);
            // But we need to remap root -> nested_path for the inner arena
            // Actually: the inner blueprint's wires use paths relative to
            // its own root. We need to process inner wires with prefix=nested_path
            // and map /:port_name -> nested_path:port_name
            nested_signals[arena_->make_port(nested_path, iface_port.name)] = it->second;
        }
    }

    // Process inner wires (these use relative paths -- root-relative)
    // We need to rewrite them to be prefixed with nested_path
    for (auto const& wire : bp_to_visit->wires()) {
        Path src = remap_path(wire.source, nested_path, interner);
        Path tgt = remap_path(wire.target, nested_path, interner);

        SignalIndex src_sig = get_or_create_signal(
            src, wire.domain, nested_signals, out);
        SignalIndex tgt_sig = get_or_create_signal(
            tgt, wire.domain, nested_signals, out);

        if (src_sig != tgt_sig) {
            merge_signals(src_sig, tgt_sig, nested_signals, out);
        }
    }

    // Merge nested_signals back into parent signals
    for (auto const& [path, sig] : nested_signals) {
        signals[path] = sig;
    }

    // Visit inner nodes with nested_path as prefix
    for (auto const& node : bp_to_visit->nodes()) {
        emit_component(node, nested_path, nested_signals, out, interner);
    }

    // Recurse into nested-of-nested
    for (auto const& inner_nested : bp_to_visit->nested()) {
        visit_nested(inner_nested, nested_path, nested_signals, out, interner);
    }
}
```

Add helper `remap_path` that prefixes an inner-blueprint relative path with the nested instance path:

```cpp
Path Flattener::remap_path(Path inner_path, Path nested_prefix,
                            ui::StringInterner& interner) {
    // Inner path is relative to inner blueprint's root.
    // E.g., inner = /r1:in, nested_prefix = /sub1
    // Result should be /sub1/r1:in
    //
    // If inner path is a root-level port (e.g., /:in), it maps to
    // nested_prefix:in (the interface port on the nested instance)

    if (inner_path.kind() == PathKind::Root) {
        return nested_prefix;
    }

    // Collect path segments from leaf to root
    std::vector<std::pair<PathKind, ui::InternedId>> segments;
    Path current = inner_path;
    while (current.kind() != PathKind::Root) {
        segments.push_back({current.kind(), current.segment()});
        current = arena_->parent(current);
    }

    // Rebuild from nested_prefix
    Path result = nested_prefix;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        switch (it->first) {
            case PathKind::Node:
                result = arena_->make_node(result, it->second);
                break;
            case PathKind::Port:
                result = arena_->make_port(result, it->second);
                break;
            case PathKind::Nested:
                result = arena_->make_nested(result, it->second);
                break;
            case PathKind::Wire:
                result = arena_->make_wire(result, it->second);
                break;
            default:
                break;
        }
    }
    return result;
}
```

Declare in header:

```cpp
    void visit_nested(
        Blueprint::Nested const& nested,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out,
        ui::StringInterner& interner);

    Path remap_path(Path inner_path, Path nested_prefix,
                     ui::StringInterner& interner);
```

Build. Run. Pass.

**Note:** If `remap_path` or `visit_nested` exceed 60 lines, split into smaller helpers immediately.

### Step 6.7: Flatten two levels of nesting

**Write test first:**

```cpp
TEST(Flattener, TwoLevelNesting) {
     ui::StringInterner interner;
     bp2::PathArena arena(interner);
     TypeRegistry reg = load_type_registry("library/");

    // Inner-most blueprint: single resistor with interface {in, out}
    bp2::Blueprint inner = make_resistor_sub(interner, reg);

    // Middle blueprint: wraps inner in a nested instance, has its own interface
    bp2::Blueprint middle;
    middle = middle.with_id(interner.intern("middle"))
                   .with_interface(bp2::Interface({
                       {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
                       {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
                   }));
    bp2::Blueprint::Nested inner_nested;
    inner_nested.id = interner.intern("inner1");
    inner_nested.embedded = true;
    inner_nested.inline_def = std::make_unique<bp2::Blueprint>(inner);
    inner_nested.iface = inner.iface();
    middle = middle.with_nested(std::move(inner_nested));
    // Wire: /:in -> /inner1:in, /inner1:out -> /:out
    // (build these wires similar to Step 6.6)
    // ... (full wire construction omitted for brevity, follow same pattern)

    // Root: Battery -> middle_inst -> LED
    bp2::Blueprint root;
    // ... (construct root with middle as embedded nested, wire endpoints)

    bp2::Flattener flattener(reg);
    bp2::FlatNetlist netlist = flattener.flatten(root, interner);

    // Expect 3 components: bat1, middle_inst/inner1/r1, led1
    EXPECT_EQ(netlist.components.size(), 3u);
}
```

Write the helper function `make_resistor_sub` at the top of the test file to avoid repetition. It returns a `bp2::Blueprint` with one Resistor node and interface {in, out} with internal wires.

Build. Run. Pass (the recursive visit_nested handles this).

### Step 6.8: Flatten with params forwarded

**Write test first:**

```cpp
TEST(Flattener, ParamsPreserved) {
     ui::StringInterner interner;
     auto reg = load_type_registry("library/");

    bp2::Blueprint bp;
    bp2::Blueprint::Node bat;
    bat.id = interner.intern("bat1");
    bat.type = interner.intern("Battery");
    bat.iface = reg.interface_of(interner.intern("Battery"));
    bat.params["v_nominal"] = 28.0f;
    bat.params["capacity"] = 24.0f;
    bp = bp.with_node(std::move(bat));

    bp2::Flattener flattener(reg);
    bp2::FlatNetlist netlist = flattener.flatten(bp, interner);

    ASSERT_EQ(netlist.components.size(), 1u);
    EXPECT_FLOAT_EQ(netlist.components[0].params.at("v_nominal"), 28.0f);
    EXPECT_FLOAT_EQ(netlist.components[0].params.at("capacity"), 24.0f);
}
```

Build. Run. Should pass (emit_component copies params).

## Final Verification

```bash
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest --output-on-failure
```

## Files Created This Phase

```
src/blueprint_v2/flattener/flat_netlist.h
src/blueprint_v2/flattener/flattener.h
src/blueprint_v2/flattener/flattener.cpp
tests/blueprint_v2/test_flattener.cpp
```

## Lines Modified

- `src/blueprint_v2/CMakeLists.txt`: add `flattener/flattener.cpp`
- `tests/CMakeLists.txt`: add `bp2_flattener_tests` target

## Domain Note: Logical Components on Electrical Layer

Some components declared `Domain::Logical` (e.g., `Add`, `Multiply`, `LUT`) share the `st.across[]` signal array with `Domain::Electrical` components. During flattening, these get their own signals like any other component. The domain tag on the signal is set from the wire's domain. The Flattener does NOT special-case logical-on-electrical -- it treats all domains uniformly. The solver's domain-dispatch logic (which runs logical after SOR convergence) is unchanged by this migration and remains in the JIT solver / AOT codegen. This is intentional: the Flattener's job is to produce a flat netlist; domain scheduling is the solver's job.
Historical document note: this phase predates the #166 architecture cleanup. References to `src/json_parser/*`, `json_parser`, and `load_type_registry()` are historical and do not describe the current architecture.
