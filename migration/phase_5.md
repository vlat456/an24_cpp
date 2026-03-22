# Phase 5: BlueprintCodec (JSON Serialization)

## Goal

Create a single, bidirectional JSON codec for `bp2::Blueprint`. This replaces three old paths: `to_flat()`, `serialize_flat_blueprint()`, and `to_simulator_json()`. One codec. One format. Round-trip tested.

The on-disk format is JSON, version `"3.0"` (to distinguish from the old `"version": 2` FlatBlueprint format). The codec does NOT know about the old format -- that is the bridge layer's job (Phase 7).

## Files To Create

```
src/blueprint_v2/codec/blueprint_codec.h      <- BlueprintCodec class
src/blueprint_v2/codec/blueprint_codec.cpp    <- encode/decode implementation
tests/blueprint_v2/test_codec.cpp             <- all tests for this phase
```

## Prerequisites

- Phase 3 complete (Blueprint, Node, Wire, Nested)
- Phase 4 complete (TypeRegistry)
- `nlohmann/json` available (already a project dependency)

## Step-by-Step Instructions

### Step 5.1: CMake setup

1. Add `codec/blueprint_codec.cpp` to `src/blueprint_v2/CMakeLists.txt`:

```cmake
add_library(blueprint_v2 STATIC
    path/path.cpp
    interface/interface.cpp
    blueprint/blueprint.cpp
    registry/type_registry.cpp
    codec/blueprint_codec.cpp
)
```

2. Add test target in `tests/CMakeLists.txt`:

```cmake
add_executable(bp2_codec_tests
    blueprint_v2/test_codec.cpp
)
target_include_directories(bp2_codec_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(bp2_codec_tests PRIVATE
    blueprint_v2
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(bp2_codec_tests)
```

3. Create placeholder files:
   - `src/blueprint_v2/codec/blueprint_codec.h`: `#pragma once` + `namespace bp2 {}`
   - `src/blueprint_v2/codec/blueprint_codec.cpp`: `#include "blueprint_codec.h"`
   - `tests/blueprint_v2/test_codec.cpp`: placeholder test

4. Build. Placeholder passes.

### Step 5.2: Encode empty blueprint

**Write test first** in `test_codec.cpp`:

```cpp
#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include <nlohmann/json.hpp>

TEST(BlueprintCodec, EncodeEmptyBlueprint) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test_bp"));

    std::string json_str = bp2::BlueprintCodec::encode(bp, interner);
    auto j = nlohmann::json::parse(json_str);

    EXPECT_EQ(j["version"], "3.0");
    EXPECT_EQ(j["id"], "test_bp");
    EXPECT_TRUE(j["nodes"].empty());
    EXPECT_TRUE(j["wires"].empty());
    EXPECT_TRUE(j["nested"].empty());
}
```

Build. Confirm fail (no `BlueprintCodec`).

**Write production code** in `blueprint_codec.h`:

```cpp
#pragma once
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/registry/type_registry.h"
#include "ui/core/interned_id.h"
#include "blueprint_v2/path/path.h"
#include <string>
#include <optional>

namespace bp2 {

struct DecodeError {
    std::string message;
    int line = -1;
};

class BlueprintCodec {
public:
    /// Serialize Blueprint -> JSON string (pretty-printed)
    static std::string encode(Blueprint const& bp,
                              ui::StringInterner const& interner);

    /// Deserialize JSON string -> Blueprint
    static std::optional<Blueprint> decode(
        std::string_view json,
        ui::StringInterner& interner,
        TypeRegistry const& registry,
        DecodeError* error_out = nullptr);
};

} // namespace bp2
```

**Write implementation** in `blueprint_codec.cpp`. Start with `encode`:

```cpp
#include "blueprint_codec.h"
#include <nlohmann/json.hpp>

namespace bp2 {

std::string BlueprintCodec::encode(Blueprint const& bp,
                                    ui::StringInterner const& interner) {
    nlohmann::json j;
    j["version"] = "3.0";
    j["id"] = std::string(interner.resolve(bp.id()));
    j["display_name"] = bp.display_name();
    j["interface"] = encode_interface(bp.iface(), interner);
    j["nodes"] = encode_nodes(bp.nodes(), interner);
    j["wires"] = encode_wires(bp.wires(), interner);
    j["nested"] = encode_nested(bp.nested(), interner);
    return j.dump(2);
}

} // namespace bp2
```

This won't compile yet because the `encode_*` helpers don't exist. Add them as private static methods declared in the header (or as free functions in the .cpp). Use free functions in an anonymous namespace in the .cpp to keep the header clean. Rewrite `encode` to call them:

In `blueprint_codec.cpp`, inside anonymous namespace:

```cpp
namespace {

nlohmann::json encode_interface(Interface const& iface,
                                 ui::StringInterner const& interner) {
    auto arr = nlohmann::json::array();
    for (auto const& port : iface) {
        nlohmann::json p;
        p["name"] = std::string(interner.resolve(port.name));
        p["domain"] = domain_to_string(port.domain);
        p["direction"] = direction_to_string(port.direction);
        arr.push_back(p);
    }
    return arr;
}

nlohmann::json encode_nodes(std::vector<Blueprint::Node> const& nodes,
                             ui::StringInterner const& interner) {
    auto arr = nlohmann::json::array();
    for (auto const& node : nodes) {
        nlohmann::json n;
        n["id"] = std::string(interner.resolve(node.id));
        n["type"] = std::string(interner.resolve(node.type));
        n["position"] = {{"x", node.x}, {"y", node.y}};
        if (!node.params.empty()) {
            nlohmann::json params;
            for (auto const& [k, v] : node.params) {
                params[k] = v;
            }
            n["params"] = params;
        }
        arr.push_back(n);
    }
    return arr;
}

} // anonymous namespace
```

You will also need `domain_to_string` and `direction_to_string` helpers. Add them to the anonymous namespace:

```cpp
std::string domain_to_string(Domain d) {
    switch (d) {
        case Domain::Electrical: return "Electrical";
        case Domain::Logical:    return "Logical";
        case Domain::Mechanical: return "Mechanical";
        case Domain::Hydraulic:  return "Hydraulic";
        case Domain::Thermal:    return "Thermal";
        default:                 return "Electrical";
    }
}

std::string direction_to_string(Direction d) {
    switch (d) {
        case Direction::Input:  return "Input";
        case Direction::Output: return "Output";
        case Direction::InOut:  return "InOut";
    }
    return "Input";
}
```

For `encode_wires`: Each wire stores `source` and `target` as `Path` objects. To serialize, we need the `PathArena` to call `to_string()`. **Decision:** The encoder needs a `PathArena const&` parameter. Update the `encode` signature:

```cpp
static std::string encode(Blueprint const& bp,
                          ui::StringInterner const& interner,
                          PathArena const& arena);
```

Update the test accordingly to create a `PathArena`. But for an empty blueprint, wires are empty, so a default arena works.

**Update the test:**

```cpp
TEST(BlueprintCodec, EncodeEmptyBlueprint) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test_bp"));

    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto j = nlohmann::json::parse(json_str);

    EXPECT_EQ(j["version"], "3.0");
    EXPECT_EQ(j["id"], "test_bp");
    EXPECT_TRUE(j["nodes"].empty());
    EXPECT_TRUE(j["wires"].empty());
    EXPECT_TRUE(j["nested"].empty());
}
```

Build. Run. Pass.

### Step 5.3: Encode nodes with params

**Write test first:**

```cpp
TEST(BlueprintCodec, EncodeNodesWithParams) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test"));

    bp2::Blueprint::Node node;
    node.id = interner.intern("bat1");
    node.type = interner.intern("Battery");
    node.x = 100.0f;
    node.y = 200.0f;
    node.params["v_nominal"] = 28.0f;
    node.params["capacity"] = 24.0f;
    bp = bp.with_node(std::move(node));

    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto j = nlohmann::json::parse(json_str);

    ASSERT_EQ(j["nodes"].size(), 1u);
    auto& n = j["nodes"][0];
    EXPECT_EQ(n["id"], "bat1");
    EXPECT_EQ(n["type"], "Battery");
    EXPECT_FLOAT_EQ(n["position"]["x"].get<float>(), 100.0f);
    EXPECT_FLOAT_EQ(n["params"]["v_nominal"].get<float>(), 28.0f);
}
```

Build. Run. This should pass if Step 5.2's `encode_nodes` was implemented correctly. If not, fix until it passes.

### Step 5.4: Encode wires

**Write test first:**

```cpp
TEST(BlueprintCodec, EncodeWires) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test"));

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("bat1")),
        interner.intern("v_out")
    );
    wire.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("r1")),
        interner.intern("in")
    );
    wire.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(wire));

    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto j = nlohmann::json::parse(json_str);

    ASSERT_EQ(j["wires"].size(), 1u);
    auto& w = j["wires"][0];
    EXPECT_EQ(w["id"], "w1");
    EXPECT_EQ(w["source"], "/bat1:v_out");
    EXPECT_EQ(w["target"], "/r1:in");
}
```

Build. Confirm fail (no `encode_wires` or wire encoding logic).

**Write production code.** Add `encode_wires` to the anonymous namespace:

```cpp
nlohmann::json encode_wires(std::vector<Blueprint::Wire> const& wires,
                             ui::StringInterner const& interner,
                             PathArena const& arena) {
    auto arr = nlohmann::json::array();
    for (auto const& wire : wires) {
        nlohmann::json w;
        w["id"] = std::string(interner.resolve(wire.id));
        w["source"] = arena.to_string(wire.source);
        w["target"] = arena.to_string(wire.target);
        arr.push_back(w);
    }
    return arr;
}
```

Update the top-level `encode` to pass `arena` to `encode_wires`.

Build. Run. Pass.

### Step 5.5: Encode nested blueprints (reference mode)

**Write test first:**

```cpp
TEST(BlueprintCodec, EncodeNestedReference) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("root"));

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("power_system");
    nested.embedded = false;
    nested.x = 300.0f;
    nested.y = 400.0f;
    bp = bp.with_nested(std::move(nested));

    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto j = nlohmann::json::parse(json_str);

    ASSERT_EQ(j["nested"].size(), 1u);
    auto& n = j["nested"][0];
    EXPECT_EQ(n["id"], "sub1");
    EXPECT_EQ(n["blueprint"], "power_system");
    EXPECT_EQ(n["embedded"], false);
    EXPECT_FALSE(n.contains("definition"));
}
```

Build. Confirm fail.

**Write production code.** Add `encode_nested` to the anonymous namespace:

```cpp
nlohmann::json encode_nested(std::vector<Blueprint::Nested> const& nested_vec,
                              ui::StringInterner const& interner,
                              PathArena const& arena) {
    auto arr = nlohmann::json::array();
    for (auto const& nested : nested_vec) {
        nlohmann::json n;
        n["id"] = std::string(interner.resolve(nested.id));
        n["blueprint"] = std::string(interner.resolve(nested.blueprint_id));
        n["embedded"] = nested.embedded;
        n["position"] = {{"x", nested.x}, {"y", nested.y}};
        if (nested.embedded && nested.inline_def) {
            n["definition"] = nlohmann::json::parse(
                BlueprintCodec::encode(*nested.inline_def, interner, arena)
            );
        }
        arr.push_back(n);
    }
    return arr;
}
```

Build. Run. Pass.

### Step 5.6: Encode embedded nested blueprint

**Write test first:**

```cpp
TEST(BlueprintCodec, EncodeNestedEmbedded) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    // Inner blueprint
    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("inner_bp"));
    bp2::Blueprint::Node inner_node;
    inner_node.id = interner.intern("r1");
    inner_node.type = interner.intern("Resistor");
    inner = inner.with_node(std::move(inner_node));

    // Outer blueprint with embedded nested
    bp2::Blueprint outer;
    outer = outer.with_id(interner.intern("outer"));
    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.embedded = true;
    nested.inline_def = std::make_unique<bp2::Blueprint>(inner);
    outer = outer.with_nested(std::move(nested));

    std::string json_str = bp2::BlueprintCodec::encode(outer, interner, arena);
    auto j = nlohmann::json::parse(json_str);

    ASSERT_EQ(j["nested"].size(), 1u);
    auto& n = j["nested"][0];
    EXPECT_TRUE(n["embedded"].get<bool>());
    EXPECT_TRUE(n.contains("definition"));
    EXPECT_EQ(n["definition"]["id"], "inner_bp");
    EXPECT_EQ(n["definition"]["nodes"].size(), 1u);
}
```

Build. Run. Should pass if step 5.5 handled the `inline_def` path.

### Step 5.7: Decode empty blueprint

**Write test first:**

```cpp
TEST(BlueprintCodec, DecodeEmptyBlueprint) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;

    std::string json = R"({
        "version": "3.0",
        "id": "test_bp",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": []
    })";

    bp2::DecodeError err;
    auto result = bp2::BlueprintCodec::decode(json, interner, reg, &err);
    ASSERT_TRUE(result.has_value()) << err.message;
    EXPECT_EQ(interner.resolve(result->id()), "test_bp");
    EXPECT_EQ(result->display_name(), "Test");
    EXPECT_TRUE(result->nodes().empty());
}
```

Build. Confirm fail (no `decode` implementation).

**Write production code** in `blueprint_codec.cpp`:

```cpp
std::optional<Blueprint> BlueprintCodec::decode(
    std::string_view json_str,
    ui::StringInterner& interner,
    TypeRegistry const& registry,
    DecodeError* error_out) {
    try {
        auto j = nlohmann::json::parse(json_str);
        return decode_blueprint(j, interner, registry);
    } catch (std::exception const& e) {
        if (error_out) error_out->message = e.what();
        return std::nullopt;
    }
}
```

Add `decode_blueprint` to the anonymous namespace:

```cpp
Blueprint decode_blueprint(nlohmann::json const& j,
                            ui::StringInterner& interner,
                            TypeRegistry const& registry) {
    Blueprint bp;
    if (j.contains("id") && j["id"].is_string()) {
        bp = bp.with_id(interner.intern(j["id"].get<std::string>()));
    }
    if (j.contains("display_name") && j["display_name"].is_string()) {
        bp = bp.with_display_name(j["display_name"].get<std::string>());
    }
    if (j.contains("interface") && j["interface"].is_array()) {
        bp = bp.with_interface(decode_interface(j["interface"], interner));
    }
    if (j.contains("nodes") && j["nodes"].is_array()) {
        bp = decode_nodes(bp, j["nodes"], interner);
    }
    if (j.contains("wires") && j["wires"].is_array()) {
        bp = decode_wires(bp, j["wires"], interner);
    }
    if (j.contains("nested") && j["nested"].is_array()) {
        bp = decode_nested(bp, j["nested"], interner, registry);
    }
    return bp;
}
```

Add the required helper functions `decode_interface`, `decode_nodes`, `decode_wires`, `decode_nested` to the anonymous namespace. Each is straightforward JSON field extraction. See below for each.

`decode_interface`:

```cpp
Interface decode_interface(nlohmann::json const& arr,
                            ui::StringInterner& interner) {
    std::vector<PortDescriptor> ports;
    for (auto const& p : arr) {
        PortDescriptor pd;
        pd.name = interner.intern(p["name"].get<std::string>());
        pd.domain = string_to_domain(p["domain"].get<std::string>());
        pd.direction = string_to_direction(p["direction"].get<std::string>());
        ports.push_back(pd);
    }
    return Interface(std::move(ports));
}
```

`string_to_domain` and `string_to_direction` are the reverse of the encode helpers. Add them to the anonymous namespace.

`decode_nodes`:

```cpp
Blueprint decode_nodes(Blueprint bp, nlohmann::json const& arr,
                        ui::StringInterner& interner) {
    for (auto const& n : arr) {
        Blueprint::Node node;
        node.id = interner.intern(n["id"].get<std::string>());
        node.type = interner.intern(n["type"].get<std::string>());
        if (n.contains("position")) {
            node.x = n["position"].value("x", 0.0f);
            node.y = n["position"].value("y", 0.0f);
        }
        if (n.contains("params") && n["params"].is_object()) {
            for (auto& [key, val] : n["params"].items()) {
                node.params[key] = val.get<float>();
            }
        }
        bp = bp.with_node(std::move(node));
    }
    return bp;
}
```

`decode_wires`: Wire paths need to be parsed. Use `PathArena::parse()`. The decoder needs a `PathArena` -- create one internally:

```cpp
Blueprint decode_wires(Blueprint bp, nlohmann::json const& arr,
                        ui::StringInterner& interner) {
    PathArena arena(interner);
    for (auto const& w : arr) {
        Blueprint::Wire wire;
        wire.id = interner.intern(w["id"].get<std::string>());
        auto src = arena.parse(w["source"].get<std::string>());
        auto tgt = arena.parse(w["target"].get<std::string>());
        if (src) wire.source = *src;
        if (tgt) wire.target = *tgt;
        bp = bp.with_wire(std::move(wire));
    }
    return bp;
}
```

`decode_nested`:

```cpp
Blueprint decode_nested(Blueprint bp, nlohmann::json const& arr,
                         ui::StringInterner& interner,
                         TypeRegistry const& registry) {
    for (auto const& n : arr) {
        Blueprint::Nested nested;
        nested.id = interner.intern(n["id"].get<std::string>());
        if (n.contains("blueprint") && n["blueprint"].is_string()) {
            std::string bp_id_str = n["blueprint"].get<std::string>();
            if (!bp_id_str.empty()) {
                nested.blueprint_id = interner.intern(bp_id_str);
            }
        }
        nested.embedded = n.value("embedded", false);
        if (n.contains("position")) {
            nested.x = n["position"].value("x", 0.0f);
            nested.y = n["position"].value("y", 0.0f);
        }
        if (nested.embedded && n.contains("definition")) {
            auto inner = decode_blueprint(n["definition"], interner, registry);
            nested.inline_def = std::make_unique<Blueprint>(std::move(inner));
        }
        // Resolve interface from registry if reference mode
        if (!nested.embedded && !nested.blueprint_id.empty()) {
            auto* entry = registry.find(nested.blueprint_id);
            if (entry) {
                nested.iface = entry->iface;
            }
        }
        bp = bp.with_nested(std::move(nested));
    }
    return bp;
}
```

Build. Run. Pass.

### Step 5.8: Decode nodes with params

**Write test first:**

```cpp
TEST(BlueprintCodec, DecodeNodesWithParams) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "nodes": [
            {
                "id": "bat1",
                "type": "Battery",
                "position": {"x": 100.0, "y": 200.0},
                "params": {"v_nominal": 28.0, "capacity": 24.0}
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, reg);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->nodes().size(), 1u);
    auto& node = result->nodes()[0];
    EXPECT_EQ(interner.resolve(node.id), "bat1");
    EXPECT_EQ(interner.resolve(node.type), "Battery");
    EXPECT_FLOAT_EQ(node.x, 100.0f);
    EXPECT_FLOAT_EQ(node.params.at("v_nominal"), 28.0f);
}
```

Build. Run. Should pass from Step 5.7's `decode_nodes`.

### Step 5.9: Decode wires

**Write test first:**

```cpp
TEST(BlueprintCodec, DecodeWires) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "nodes": [],
        "wires": [
            {"id": "w1", "source": "/bat1:v_out", "target": "/r1:in"}
        ],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, reg);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->wires().size(), 1u);
    auto& wire = result->wires()[0];
    EXPECT_EQ(interner.resolve(wire.id), "w1");
    EXPECT_EQ(wire.source.kind(), bp2::PathKind::Port);
    EXPECT_EQ(interner.resolve(wire.source.segment()), "v_out");
}
```

Build. Run. Pass (from Step 5.7's decode_wires).

### Step 5.10: Round-trip test

**Write test first:**

```cpp
TEST(BlueprintCodec, RoundTripSimpleBlueprint) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(interner);

    // Build a blueprint
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("rt_test"))
           .with_display_name("Round-Trip Test");

    bp2::Blueprint::Node bat;
    bat.id = interner.intern("bat1");
    bat.type = interner.intern("Battery");
    bat.x = 10.0f;
    bat.y = 20.0f;
    bat.params["v_nominal"] = 28.0f;
    bp = bp.with_node(std::move(bat));

    bp2::Blueprint::Node res;
    res.id = interner.intern("r1");
    res.type = interner.intern("Resistor");
    res.x = 30.0f;
    res.y = 20.0f;
    bp = bp.with_node(std::move(res));

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
    bp = bp.with_wire(std::move(w));

    // Encode
    std::string json1 = bp2::BlueprintCodec::encode(bp, interner, arena);

    // Decode
    auto decoded = bp2::BlueprintCodec::decode(json1, interner, reg);
    ASSERT_TRUE(decoded.has_value());

    // Re-encode
    bp2::PathArena arena2(interner);
    std::string json2 = bp2::BlueprintCodec::encode(*decoded, interner, arena2);

    // Compare JSON (not string equality -- parse and compare)
    auto j1 = nlohmann::json::parse(json1);
    auto j2 = nlohmann::json::parse(json2);
    EXPECT_EQ(j1, j2);
}
```

Build. Run. Pass.

**Note on round-trip:** String-exact equality of JSON may differ due to key ordering. Compare parsed JSON objects instead (nlohmann::json has `operator==`).

### Step 5.11: Decode malformed JSON

**Write test first:**

```cpp
TEST(BlueprintCodec, DecodeInvalidJsonReturnsNullopt) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    auto result = bp2::BlueprintCodec::decode("not json", interner, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(err.message.empty());
}

TEST(BlueprintCodec, DecodeMissingIdStillWorks) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;

    std::string json = R"({"version": "3.0", "nodes": [], "wires": [], "nested": []})";
    auto result = bp2::BlueprintCodec::decode(json, interner, reg);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->id().empty());
}
```

Build. Run. Pass (the `decode` already handles missing fields gracefully via `j.contains()` checks).

### Step 5.12: Encode/decode interface ports

**Write test first:**

```cpp
TEST(BlueprintCodec, RoundTripInterface) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("iface_test"))
           .with_interface(bp2::Interface({
               {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input},
               {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output},
               {interner.intern("gnd"), Domain::Electrical, bp2::Direction::InOut},
           }));

    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto decoded = bp2::BlueprintCodec::decode(json_str, interner, reg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->iface().size(), 3u);

    auto it = decoded->iface().begin();
    EXPECT_EQ(interner.resolve(it->name), "v_in");
    EXPECT_EQ(it->domain, Domain::Electrical);
    EXPECT_EQ(it->direction, bp2::Direction::Input);
}
```

Build. Run. Pass.

## Final Verification

```bash
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest --output-on-failure
```

## Files Created This Phase

```
src/blueprint_v2/codec/blueprint_codec.h
src/blueprint_v2/codec/blueprint_codec.cpp
tests/blueprint_v2/test_codec.cpp
```

## Lines Modified

- `src/blueprint_v2/CMakeLists.txt`: add `codec/blueprint_codec.cpp`
- `tests/CMakeLists.txt`: add `bp2_codec_tests` target
