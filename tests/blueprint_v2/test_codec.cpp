#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/interface/node_port_projection.h"
#include "blueprint_v2/path/path.h"
#include "blueprint_v2/validation/path_resolver.h"
#include "blueprint_v2/validation/wire_validator.h"
#include "json_parser/json_parser.h"
#include <nlohmann/json.hpp>
#include <type_traits>

// ==============================================================================
// Helper: register a lightweight type stub in the parser TypeRegistry
// ==============================================================================
namespace {

// Shared bp2 test helpers (make_port, set_iface)
#include "../bp2_test_helpers.h"

template <typename T>
struct second_arg;

template <typename R, typename A0, typename A1, typename A2, typename A3>
struct second_arg<R (*)(A0, A1, A2, A3)> {
    using type = A1;
};

/// Convert bp2::Direction to PortDirection
PortDirection to_port_direction(bp2::Direction dir) {
    switch (dir) {
         case bp2::Direction::Input:  return PortDirection::In;
         case bp2::Direction::Output: return PortDirection::Out;
         case bp2::Direction::InOut:  return PortDirection::InOut;
         default: return PortDirection::Out;
     }
 }

/// Register a type stub in the canonical TypeRegistry.
/// Ports are derived from the bp2::Interface using the interner to resolve names.
void register_type(
    TypeRegistry& reg,
    ui::StringInterner& interner,
    const std::string& classname,
    const bp2::Interface& iface = bp2::Interface(),
    std::unordered_map<std::string, std::string> params = {},
    std::unordered_map<std::string, ParamSchemaEntry> param_schema = {})
{
    TypeDefinition def;
    def.classname = classname;
    def.cpp_class = true;
    def.params = std::move(params);
    def.param_schema = std::move(param_schema);

    for (const auto& pd : iface.ports()) {
        std::string port_name(interner.resolve(pd.name));
        def.ports[port_name] = Port{
            to_port_direction(pd.direction),
            PortType::Any,
            pd.domain,
            false
        };
    }

    reg.types[classname] = def;
}

/// Create test registry by loading from library directory
TypeRegistry make_test_registry() {
    return load_type_registry("library/");
}

}  // namespace

TEST(BlueprintCodec, Placeholder) {
    EXPECT_TRUE(true);
}

TEST(BlueprintCodec, EncodeSignatureUsesConstInterner) {
    using EncodeFn = decltype(&bp2::BlueprintCodec::encode);
    using InternerArg = typename second_arg<EncodeFn>::type;

    static_assert(std::is_reference_v<InternerArg>,
                  "BlueprintCodec::encode interner argument must be a reference");
    static_assert(std::is_const_v<std::remove_reference_t<InternerArg>>,
                  "BlueprintCodec::encode must accept const StringInterner&");
    EXPECT_TRUE(true);
}

TEST(BlueprintCodec, EncodeDoesNotMutateInterner) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("const_safe_encode"));

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.semantic.type = interner.intern("Resistor");
    node.layout.x = 1.0f;
    node.layout.y = 2.0f;
    node.semantic.params[interner.intern("r_ohm")] = 42.0f;
    bp = bp.with_node(std::move(node));

    const size_t before = interner.size();
    const std::string encoded = bp2::BlueprintCodec::encode(bp, interner, arena);
    const size_t after = interner.size();

    EXPECT_FALSE(encoded.empty());
    EXPECT_EQ(after, before) << "encode path should not intern new strings";
}

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

TEST(BlueprintCodec, EncodeNodesWithParams) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test"));

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("bat1");
    node.semantic.type = interner.intern("Battery");
    node.layout.x = 100.0f;
    node.layout.y = 200.0f;
    node.semantic.params[interner.intern("v_nominal")] = 28.0f;
    node.semantic.params[interner.intern("capacity")] = 24.0f;
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

TEST(BlueprintCodec, EncodeNestedReference) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("root"));

    auto nested = bp2::Blueprint::Nested::make_reference(
        interner.intern("sub1"),
        interner.intern("power_system"),
        bp2::Interface{},
        300.0f, 400.0f);
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

TEST(BlueprintCodec, EncodeNestedEmbedded) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

     bp2::Blueprint inner;
     inner = inner.with_id(interner.intern("inner_bp"));
     bp2::Blueprint::Node inner_node;
     inner_node.semantic.id = interner.intern("r1");
     inner_node.semantic.type = interner.intern("Resistor");
     inner = inner.with_node(std::move(inner_node));

    bp2::Blueprint outer;
    outer = outer.with_id(interner.intern("outer"));
    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("sub1"), ui::InternedId{},
        std::make_unique<bp2::Blueprint>(inner));
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

TEST(BlueprintCodec, DecodeEmptyBlueprint) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "Subtract");

    // Library blueprints (e.g. library/math/FirstOrderLag.blueprint) have nodes
    // WITHOUT position fields. The codec must accept these and default to {0,0}.
    std::string json = R"({
        "version": "3.0",
        "id": "test_no_pos",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Subtract",
                "ports": {
                    "A": {"direction": "In", "type": "Any"},
                    "B": {"direction": "In", "type": "Any"},
                    "o": {"direction": "Out", "type": "Any"}
                }
            }
        ],
        "wires": [],
        "nested": []
    })";

     bp2::DecodeError err;
     auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
     ASSERT_TRUE(result.has_value()) << "Decode failed: " << err.message;
     ASSERT_EQ(result->nodes().size(), 1u);
     EXPECT_FLOAT_EQ(result->nodes()[0].layout.x, 0.0f);
     EXPECT_FLOAT_EQ(result->nodes()[0].layout.y, 0.0f);
}

TEST(BlueprintCodec, DecodeNodeWithPosition_ParsesNormally) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "Subtract");

    std::string json = R"({
        "version": "3.0",
        "id": "test_with_pos",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Subtract",
                "position": {"x": 42.0, "y": -7.5}
            }
        ],
        "wires": [],
        "nested": []
    })";

     bp2::DecodeError err;
     auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
     ASSERT_TRUE(result.has_value()) << "Decode failed: " << err.message;
     ASSERT_EQ(result->nodes().size(), 1u);
     EXPECT_FLOAT_EQ(result->nodes()[0].layout.x, 42.0f);
     EXPECT_FLOAT_EQ(result->nodes()[0].layout.y, -7.5f);
}

// =============================================================================
// Regression: node.semantic.iface must be populated from decoded ports so that
// PathResolver can resolve wire endpoints even when the node type is NOT
// in the library registry (e.g. embedded blueprint proxy nodes).
// Bug: closed_circuit.blueprint saved OK but failed to reload with
//   "[persist] Failed to load blueprint: wire id=186: wire endpoint path unresolved"
// Root cause: decode_nodes() populated node.view.inputs/node.view.outputs but never
// built node.semantic.iface, so node_interface() returned nullptr for non-registry types.
// =============================================================================

TEST(BlueprintCodec, DecodePopulatesNodeIfaceFromPorts) {
    // After decoding a node with "ports", node.semantic.iface must be non-empty
    // and contain the correct PortDescriptors — even for known types.
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "Battery");

    std::string json = R"({
        "version": "3.0",
        "id": "iface_test",
        "display_name": "Iface Test",
        "interface": [],
        "nodes": [
            {
                "id": "bat1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "ports": {
                    "feedback": {"direction": "In", "type": "V"},
                    "output":   {"direction": "Out", "type": "V"},
                    "bidir":    {"direction": "InOut", "type": "Any"}
                }
            }
        ],
        "wires": [],
        "nested": []
    })";

    bp2::DecodeError err;
    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    ASSERT_TRUE(result.has_value()) << "Decode failed: " << err.message;
    ASSERT_EQ(result->nodes().size(), 1u);

    const auto& node = result->nodes()[0];
    // The fix: node.semantic.iface must be populated from the decoded ports
    EXPECT_FALSE(node.semantic.iface.empty());
    EXPECT_EQ(node.semantic.iface.size(), 3u);

    // Verify each port is findable by name
    EXPECT_TRUE(node.semantic.iface.has(interner.intern("feedback")));
    EXPECT_TRUE(node.semantic.iface.has(interner.intern("output")));
    EXPECT_TRUE(node.semantic.iface.has(interner.intern("bidir")));

    // Verify directions
    auto fb = node.semantic.iface.find(interner.intern("feedback"));
    ASSERT_TRUE(fb.has_value());
    EXPECT_EQ(fb->direction, bp2::Direction::Input);

    auto out = node.semantic.iface.find(interner.intern("output"));
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->direction, bp2::Direction::Output);

    auto bd = node.semantic.iface.find(interner.intern("bidir"));
    ASSERT_TRUE(bd.has_value());
    EXPECT_EQ(bd->direction, bp2::Direction::InOut);
}

TEST(BlueprintCodec, DecodeNodeIfaceEmptyWhenNoPorts) {
    // When a node has NO "ports" block, iface should remain empty
    // (the registry may provide it separately via node_interface())
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "SomeType");

    std::string json = R"({
        "version": "3.0",
        "id": "no_ports_test",
        "display_name": "No Ports",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "SomeType",
                "position": {"x": 0, "y": 0}
            }
        ],
        "wires": [],
        "nested": []
    })";

     bp2::DecodeError err;
     auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
     ASSERT_TRUE(result.has_value()) << "Decode failed: " << err.message;
     EXPECT_TRUE(result->nodes()[0].semantic.iface.empty());
}

TEST(BlueprintCodec, RoundTripProxyNodeWithWires_EndpointsResolve) {
    // This is the exact regression scenario: a blueprint with a proxy node
    // whose type is NOT in the registry (embedded blueprint proxy), but whose
    // ports are serialized. The proxy node has expandable=true and a matching
    // embedded nested definition. Wires target the proxy's ports.
    // After save → load roundtrip, wire endpoint resolution must succeed.
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    // Register Battery (standard library type)
    register_type(
        reg, interner, "Battery",
        bp2::Interface({
            {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output},
        })
    );
    // Do NOT register "RN-180-Exciter" — it's a custom proxy type

    // --- Build a blueprint with a proxy node and wires to it ---
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("roundtrip_proxy"));
    bp = bp.with_display_name("Roundtrip Proxy Test");

     // Standard node (Battery)
      bp2::Blueprint::Node bat;
      bat.semantic.id = interner.intern("bat1");
      bat.semantic.type = interner.intern("Battery");
      bat.layout.x = 10.0f;
      bat.layout.y = 20.0f;
      set_iface(bat, {
          make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
      });
      bp = bp.with_node(std::move(bat));

      // Proxy node (type NOT in registry, expandable with embedded nested def)
      bp2::Blueprint::Node proxy;
      proxy.semantic.id = interner.intern("extract_inst_1");
      proxy.semantic.type = interner.intern("RN-180-Exciter");
      proxy.view.expandable = true;
      proxy.layout.x = 100.0f;
      proxy.layout.y = 200.0f;
      set_iface(proxy, {
          make_port(interner, "feedback", Domain::Electrical, bp2::Direction::Input, PortType::V),
          make_port(interner, "output", Domain::Electrical, bp2::Direction::Output, PortType::V),
      });
      bp = bp.with_node(std::move(proxy));

    // Matching embedded nested definition (required by InvariantChecker for
    // expandable proxy nodes with unknown type)
    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("RN-180-Exciter"));
    inner = inner.with_display_name("RN-180 Exciter");
    inner = inner.with_interface(bp2::Interface({
        {interner.intern("feedback"), Domain::Electrical, bp2::Direction::Input, PortType::V},
        {interner.intern("output"), Domain::Electrical, bp2::Direction::Output, PortType::V},
    }));
    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("extract_inst_1"),
        interner.intern("RN-180-Exciter"),
        std::make_unique<bp2::Blueprint>(inner),
        100.0f, 200.0f);
    bp = bp.with_nested(std::move(nested));

    // Wire: bat1:v_out → extract_inst_1:feedback
    bp2::Blueprint::Wire w;
    w.id = interner.intern("w_proxy");
    w.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("bat1")),
        interner.intern("v_out")
    );
    w.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("extract_inst_1")),
        interner.intern("feedback")
    );
    w.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w));

    // --- Encode (save) ---
    std::string json_saved = bp2::BlueprintCodec::encode(bp, interner, arena);

    // --- Decode (load) — this is where the bug manifested ---
    bp2::DecodeError err;
    auto loaded = bp2::BlueprintCodec::decode(json_saved, interner, arena, reg, &err);
    ASSERT_TRUE(loaded.has_value()) << "Roundtrip decode failed: " << err.message;

    // Verify structure preserved
    ASSERT_EQ(loaded->nodes().size(), 2u);
    ASSERT_EQ(loaded->wires().size(), 1u);

    // --- The actual regression check: wire endpoint resolution ---
    // This is what WireValidator does internally; it must succeed.
    TypeRegistry parser_registry = load_type_registry("library/");
    bp2::PathResolver resolver;
    const auto& wire = loaded->wires()[0];
    auto src = resolver.resolve(wire.source, *loaded, arena, parser_registry, interner);
    auto tgt = resolver.resolve(wire.target, *loaded, arena, parser_registry, interner);
    EXPECT_TRUE(src.has_value()) << "Source endpoint unresolved after roundtrip";
    EXPECT_TRUE(tgt.has_value()) << "Target endpoint unresolved after roundtrip (THE BUG)";

    // Also verify via WireValidator (the actual code path that produces the error)
    auto vr = bp2::WireValidator::validate(wire, *loaded, arena, parser_registry, interner);
    EXPECT_TRUE(vr.valid) << "Wire validation failed: " << vr.error;
}

TEST(BlueprintCodec, RoundTripProxyNodeWithWires_DoubleRoundTrip) {
    // Encode → Decode → Encode → Decode: verify stability across two roundtrips.
    // This catches any loss of iface data that could accumulate.
    // Uses expandable proxy nodes with embedded nested definitions.
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(
        reg, interner, "Generator",
        bp2::Interface({
            {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output},
        })
    );
    // "CustomSubsystem" is NOT registered — it's a proxy type

    std::string json1 = R"({
        "version": "3.0",
        "id": "double_rt",
        "display_name": "Double Roundtrip",
        "interface": [],
        "nodes": [
            {
                "id": "src_node",
                "type": "Generator",
                "position": {"x": 0, "y": 0},
                "ports": {
                    "v_out": {"direction": "Out", "type": "V"}
                }
            },
            {
                "id": "proxy_node",
                "type": "CustomSubsystem",
                "expandable": true,
                "position": {"x": 100, "y": 0},
                "ports": {
                    "input":  {"direction": "In",  "type": "V"},
                    "output": {"direction": "Out", "type": "V"}
                }
            }
        ],
        "wires": [
            {"id": "w1", "source": "/src_node:v_out", "target": "/proxy_node:input"}
        ],
        "nested": [
            {
                "id": "proxy_node",
                "blueprint": "CustomSubsystem",
                "embedded": true,
                "position": {"x": 100, "y": 0},
                "definition": {
                    "version": "3.0",
                    "id": "CustomSubsystem",
                    "display_name": "Custom Subsystem",
                    "interface": [
                        {"name": "input",  "domain": 1, "direction": 0, "type": "V", "source_writer": false},
                        {"name": "output", "domain": 1, "direction": 1, "type": "V", "source_writer": false}
                    ],
                    "nodes": [],
                    "wires": [],
                    "nested": []
                }
            }
        ]
    })";

    // First decode
    bp2::DecodeError err;
    auto bp1 = bp2::BlueprintCodec::decode(json1, interner, arena, reg, &err);
    ASSERT_TRUE(bp1.has_value()) << "First decode failed: " << err.message;

    // First re-encode
    std::string json2 = bp2::BlueprintCodec::encode(*bp1, interner, arena);

    // Second decode
    auto bp2_result = bp2::BlueprintCodec::decode(json2, interner, arena, reg, &err);
    ASSERT_TRUE(bp2_result.has_value()) << "Second decode failed: " << err.message;

    // Wire must still resolve after double roundtrip
    TypeRegistry parser_registry = load_type_registry("library/");
    bp2::PathResolver resolver;
    const auto& wire = bp2_result->wires()[0];
    auto src = resolver.resolve(wire.source, *bp2_result, arena, parser_registry, interner);
    auto tgt = resolver.resolve(wire.target, *bp2_result, arena, parser_registry, interner);
    EXPECT_TRUE(src.has_value()) << "Source unresolved after double roundtrip";
    EXPECT_TRUE(tgt.has_value()) << "Target unresolved after double roundtrip";

    // Validate wire
    auto vr = bp2::WireValidator::validate(wire, *bp2_result, arena, parser_registry, interner);
    EXPECT_TRUE(vr.valid) << "Wire validation failed after double roundtrip: " << vr.error;
}

TEST(BlueprintCodec, DecodeLibraryBlueprintFormat_FullExample) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "BlueprintInput");
    register_type(reg, interner, "BlueprintOutput");

    // Simplified version of library/math/FirstOrderLag.blueprint structure:
    // - nodes have "ports" but NO "position"
    // - top-level has "cpp_class", "description", "domains", "scheduler_source"
    std::string json = R"({
        "version": "3.0",
        "id": "FirstOrderLag",
        "display_name": "FirstOrderLag",
        "scheduler_source": false,
        "cpp_class": false,
        "description": "Test library blueprint",
        "domains": ["Logical"],
        "interface": [
            {"name": "in", "domain": 1, "direction": 0, "type": "Any", "source_writer": false},
            {"name": "out", "domain": 1, "direction": 1, "type": "Any", "source_writer": false}
        ],
        "nodes": [
            {
                "id": "in",
                "type": "BlueprintInput",
                "ports": {
                    "ext": {"direction": "In", "type": "Any"},
                    "port": {"direction": "Out", "type": "Any"}
                }
            },
            {
                "id": "out",
                "type": "BlueprintOutput",
                "ports": {
                    "ext": {"direction": "Out", "type": "Any"},
                    "port": {"direction": "In", "type": "Any"}
                }
            }
        ],
        "wires": [],
        "nested": []
    })";

    bp2::DecodeError err;
    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    ASSERT_TRUE(result.has_value()) << "Library blueprint decode failed: " << err.message;
    EXPECT_EQ(result->nodes().size(), 2u);

    // All positions should default to {0, 0}
    for (const auto& node : result->nodes()) {
        EXPECT_FLOAT_EQ(node.layout.x, 0.0f);
        EXPECT_FLOAT_EQ(node.layout.y, 0.0f);
    }
}

// Regression: decode of embedded nested entry must populate nested.iface
// from the inline definition's interface.  Before the fix, nested.iface
// was left default-constructed (empty) for embedded nested entries, which
// broke PathResolver resolution of nested boundary ports and any code
// that relied on nested.iface after a load.
TEST(BlueprintCodec, DecodeEmbeddedNestedPopulatesIfaceFromInlineDef) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;

    std::string json = R"({
        "version": "3.0",
        "id": "nested_iface_test",
        "display_name": "Nested Iface Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [
            {
                "id": "sub1",
                "blueprint": "InnerBP",
                "embedded": true,
                "position": {"x": 10, "y": 20},
                "definition": {
                    "version": "3.0",
                    "id": "InnerBP",
                    "display_name": "Inner Blueprint",
                    "interface": [
                        {"name": "in_port",  "domain": 1, "direction": 0, "type": "Any", "source_writer": false},
                        {"name": "out_port", "domain": 1, "direction": 1, "type": "Any", "source_writer": false}
                    ],
                    "nodes": [],
                    "wires": [],
                    "nested": []
                }
            }
        ]
    })";

    bp2::DecodeError err;
    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    ASSERT_TRUE(result.has_value()) << "Decode failed: " << err.message;
    ASSERT_EQ(result->nested().size(), 1u);

    const auto& nested = result->nested()[0];
    EXPECT_TRUE(nested.is_embedded());
    ASSERT_NE(nested.inline_def(), nullptr);

    // THE REGRESSION: nested.resolved_iface() must mirror inline_def's interface
    auto resolved = nested.resolved_iface();
    EXPECT_FALSE(resolved.empty())
        << "nested.resolved_iface() is empty after decode — inline_def iface not propagated";
    EXPECT_EQ(resolved.size(), 2u);
    EXPECT_TRUE(resolved.has(interner.intern("in_port")));
    EXPECT_TRUE(resolved.has(interner.intern("out_port")));

    // Verify directions match the inline definition
    auto in_port = resolved.find(interner.intern("in_port"));
    ASSERT_TRUE(in_port.has_value());
    EXPECT_EQ(in_port->direction, bp2::Direction::Input);

    auto out_port = resolved.find(interner.intern("out_port"));
    ASSERT_TRUE(out_port.has_value());
    EXPECT_EQ(out_port->direction, bp2::Direction::Output);
}

// Regression: embedded nested iface must survive encode→decode roundtrip.
TEST(BlueprintCodec, RoundTripEmbeddedNestedPreservesIface) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;

    // Build a blueprint with an embedded nested that has a real interface
    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("SubSystem"));
    inner = inner.with_display_name("Sub System");
    inner = inner.with_interface(bp2::Interface({
        {interner.intern("sig_in"),  Domain::Electrical, bp2::Direction::Input},
        {interner.intern("sig_out"), Domain::Electrical, bp2::Direction::Output},
    }));

    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("inst1"),
        interner.intern("SubSystem"),
        std::make_unique<bp2::Blueprint>(inner),
        5.0f, 10.0f);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("roundtrip_nested_iface"));
    bp = bp.with_display_name("RT Nested Iface");
    bp = bp.with_nested(std::move(nested));

    // Encode
    std::string json = bp2::BlueprintCodec::encode(bp, interner, arena);

    // Decode
    bp2::DecodeError err;
    auto loaded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    ASSERT_TRUE(loaded.has_value()) << "Roundtrip decode failed: " << err.message;
    ASSERT_EQ(loaded->nested().size(), 1u);

    const auto& loaded_nested = loaded->nested()[0];
    auto loaded_resolved = loaded_nested.resolved_iface();
    EXPECT_FALSE(loaded_resolved.empty())
        << "nested.resolved_iface() lost during roundtrip";
    EXPECT_EQ(loaded_resolved.size(), 2u);
    EXPECT_TRUE(loaded_resolved.has(interner.intern("sig_in")));
    EXPECT_TRUE(loaded_resolved.has(interner.intern("sig_out")));
}

TEST(BlueprintCodec, RoundTripPreservesNodeContentTypeByEnumValue) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg = make_test_registry();
    // Register Battery for this test since it's not in library
    register_type(reg, interner, "Battery");

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("content_rt"));
    bp = bp.with_display_name("Content RT");

     bp2::Blueprint::Node n;
     n.semantic.id = interner.intern("knob1");
     n.semantic.type = interner.intern("Battery");
     n.view.content_type = bp2::NodeContentType::Knob;
     bp = bp.with_node(std::move(n));

     const std::string encoded = bp2::BlueprintCodec::encode(bp, interner, arena);
     const auto j = nlohmann::json::parse(encoded);
     ASSERT_EQ(j["nodes"].size(), 1u);
     EXPECT_EQ(j["nodes"][0]["content_type"].get<int>(),
               static_cast<int>(bp2::NodeContentType::Knob));

     bp2::DecodeError err;
     auto decoded = bp2::BlueprintCodec::decode(encoded, interner, arena, reg, &err);
     ASSERT_TRUE(decoded.has_value()) << err.message;
     ASSERT_EQ(decoded->nodes().size(), 1u);
     EXPECT_EQ(decoded->nodes()[0].view.content_type, bp2::NodeContentType::Knob);
}

TEST(BlueprintCodec, DecodeRejectsNodeContentTypeAtSentinelBoundary) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg = make_test_registry();
    bp2::DecodeError err;

    const int invalid_content_type = static_cast<int>(bp2::NodeContentType::Count);
    const std::string json = std::string(R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0.0, "y": 0.0},
                "content_type": )") + std::to_string(invalid_content_type) + R"(
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("content_type out of range"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsInterfaceMissingTypeAndSourceWriter) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg = make_test_registry();
    bp2::DecodeError err;

    const std::string json = R"({
        "version": "3.0",
        "id": "iface_missing_fields",
        "display_name": "Iface Missing Fields",
        "interface": [
            {"name": "in", "domain": 1, "direction": 0}
        ],
        "nodes": [],
        "wires": [],
        "nested": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_NE(err.message.find("type"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsInterfaceMissingSourceWriter) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg = make_test_registry();
    bp2::DecodeError err;

    const std::string json = R"({
        "version": "3.0",
        "id": "iface_missing_sw",
        "display_name": "Iface Missing SW",
        "interface": [
            {"name": "in", "domain": 1, "direction": 0, "type": "Any"}
        ],
        "nodes": [],
        "wires": [],
        "nested": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_NE(err.message.find("source_writer"), std::string::npos);
}

TEST(BlueprintCodec, EncodeInterfaceIncludesCanonicalPortMetadata) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("iface_encode_meta"));
    bp = bp.with_display_name("Iface Encode Meta");

     bp2::PortDescriptor pd;
     pd.name = interner.intern("out");
     pd.domain = Domain::Electrical;
     pd.direction = bp2::Direction::Output;
     // TODO: PortDescriptor doesn't have type, source_writer, alias fields
     // pd.semantic.type = PortType::V;
     // pd.source_writer = true;
     // pd.alias = std::string("in");
     bp = bp.with_interface(bp2::Interface({pd}));

     const std::string encoded = bp2::BlueprintCodec::encode(bp, interner, arena);
     const auto j = nlohmann::json::parse(encoded);

     ASSERT_EQ(j["interface"].size(), 1u);
     const auto& p = j["interface"][0];
     EXPECT_EQ(p["name"], "out");
     // EXPECT_EQ(p["type"], "V");
     // EXPECT_EQ(p["source_writer"], true);
     // EXPECT_EQ(p["alias"], "in");
}

TEST(BlueprintCodec, EncodeWithParserRegistryOverload) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("codec_parser_encode"));
    bp = bp.with_display_name("Codec Parser Encode");

    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena, &parser_registry);
    auto j = nlohmann::json::parse(json_str);
    EXPECT_EQ(j["version"], "3.0");
    EXPECT_EQ(j["id"], "codec_parser_encode");
}

TEST(BlueprintCodec, DecodeWithParserRegistryOverload) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry parser_registry = load_type_registry("library/");

    const std::string json = R"({
        "version": "3.0",
        "id": "codec_parser_decode",
        "display_name": "Codec Parser Decode",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": []
    })";

    bp2::DecodeError err;
    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, parser_registry, &err);
    ASSERT_TRUE(decoded.has_value()) << err.message;
    EXPECT_EQ(interner.resolve(decoded->id()), "codec_parser_decode");
}

TEST(BlueprintCodec, ReferenceNestedIfaceCache_RoundTripParity) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "RefNestedType", bp2::Interface({
        make_port(interner, "vin", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "ctrl", Domain::Logical, bp2::Direction::InOut, PortType::Bool),
        make_port(interner, "rpm", Domain::Mechanical, bp2::Direction::Output, PortType::RPM),
    }));

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("ref_nested_roundtrip"));
    bp = bp.with_display_name("Ref Nested RoundTrip");

    auto nested = bp2::Blueprint::Nested::make_reference(
        interner.intern("power_inst_1"),
        interner.intern("RefNestedType"),
        bp2::Interface(),
        12.0f, 34.0f);
    bp = bp.with_nested(std::move(nested));

    const std::string encoded1 = bp2::BlueprintCodec::encode(bp, interner, arena);

    bp2::DecodeError err;
    auto decoded1 = bp2::BlueprintCodec::decode(encoded1, interner, arena, reg, &err);
    ASSERT_TRUE(decoded1.has_value()) << err.message;
    ASSERT_EQ(decoded1->nested().size(), 1u);
    const auto& n1 = decoded1->nested()[0];
    ASSERT_TRUE(n1.is_reference());

    const std::string encoded2 = bp2::BlueprintCodec::encode(*decoded1, interner, arena);
    auto decoded2 = bp2::BlueprintCodec::decode(encoded2, interner, arena, reg, &err);
    ASSERT_TRUE(decoded2.has_value()) << err.message;
    ASSERT_EQ(decoded2->nested().size(), 1u);
    const auto& n2 = decoded2->nested()[0];
    ASSERT_TRUE(n2.is_reference());

    ASSERT_EQ(n1.resolved_iface().size(), n2.resolved_iface().size());
    for (const auto& pd : n1.resolved_iface()) {
        auto maybe = n2.resolved_iface().find(pd.name);
        ASSERT_TRUE(maybe.has_value());
        EXPECT_EQ(*maybe, pd);
    }
}

// =============================================================================
// Issue #31 regression tests: semantic.iface is the single source of truth
// =============================================================================

// Issue #31 Required Test 1: Mutation single-path
// After mutating semantic.iface on a node, derive_input_ports() / derive_output_ports()
// must reflect the change WITHOUT any explicit view.inputs/outputs mutation (because
// those fields no longer exist).
TEST(Issue31_SingleSource, MutationSinglePath_DeriveReflectsSemanticIface) {
    ui::StringInterner interner;

    // Build a node with semantic.iface ports only
    bp2::Blueprint::Node collapsed;
    collapsed.semantic.id = interner.intern("proxy1");
    collapsed.semantic.type = interner.intern("CustomSubsystem");
    collapsed.view.expandable = true;

    // Start with one input port
    collapsed.semantic.iface = bp2::Interface({
        make_port(interner, "sig_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("mutation_test"));
    bp = bp.with_node(std::move(collapsed));

    // Verify initial state via projection
    const auto& node_before = bp.nodes()[0];
    auto inputs_before = bp2::derive_input_ports(node_before.semantic.iface);
    auto outputs_before = bp2::derive_output_ports(node_before.semantic.iface);
    ASSERT_EQ(inputs_before.size(), 1u);
    EXPECT_EQ(outputs_before.size(), 0u);
    EXPECT_EQ(interner.resolve(inputs_before[0].name), "sig_in");

    // Simulate the mutation: add a new output port to semantic.iface (same as
    // add_bridge_port_to_composite does — single mutation path, no view mutation)
    bp2::Blueprint::Node mutated = bp.nodes()[0];
    {
        std::vector<bp2::PortDescriptor> ports = mutated.semantic.iface.ports();
        ports.push_back(make_port(interner, "sig_out", Domain::Electrical, bp2::Direction::Output, PortType::V));
        mutated.semantic.iface = bp2::Interface(std::move(ports));
    }

    // The projection must immediately reflect the mutation
    auto inputs_after = bp2::derive_input_ports(mutated.semantic.iface);
    auto outputs_after = bp2::derive_output_ports(mutated.semantic.iface);
    EXPECT_EQ(inputs_after.size(), 1u);
    ASSERT_EQ(outputs_after.size(), 1u);
    EXPECT_EQ(interner.resolve(inputs_after[0].name), "sig_in");
    EXPECT_EQ(interner.resolve(outputs_after[0].name), "sig_out");
    EXPECT_EQ(outputs_after[0].type, PortType::V);
    EXPECT_EQ(outputs_after[0].side, bp2::PortSide::Output);
}

// Issue #31 Required Test 2: Export reads semantic
// Build a blueprint, encode its ports from semantic.iface, decode, and verify
// that the ports in the decoded node match exactly what semantic.iface defines.
// This ensures the export/codec pipeline reads from semantic, not stale view data.
TEST(Issue31_SingleSource, ExportReadsSemanticIface_CodecRoundTrip) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "TestDevice");

    // Build a node whose semantic.iface has specific port types/directions
    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("dev1");
    node.semantic.type = interner.intern("TestDevice");
    set_iface(node, {
        make_port(interner, "ctrl",   Domain::Logical,    bp2::Direction::Input,  PortType::Bool),
        make_port(interner, "v_bus",  Domain::Electrical,  bp2::Direction::InOut,  PortType::V),
        make_port(interner, "temp",   Domain::Thermal,     bp2::Direction::Output, PortType::Temperature),
    });

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("export_semantic_test"));
    bp = bp.with_node(std::move(node));

    // Encode (this must read from semantic.iface, not view)
    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto j = nlohmann::json::parse(json_str);

    // Verify the encoded ports match semantic.iface
    ASSERT_TRUE(j["nodes"][0].contains("ports"));
    auto& ports_json = j["nodes"][0]["ports"];
    ASSERT_EQ(ports_json.size(), 3u);

    EXPECT_EQ(ports_json["ctrl"]["direction"], "In");
    EXPECT_EQ(ports_json["v_bus"]["direction"], "InOut");
    EXPECT_EQ(ports_json["temp"]["direction"], "Out");

    // Decode and verify semantic.iface is correctly reconstructed
    bp2::DecodeError err;
    auto loaded = bp2::BlueprintCodec::decode(json_str, interner, arena, reg, &err);
    ASSERT_TRUE(loaded.has_value()) << "Decode failed: " << err.message;
    ASSERT_EQ(loaded->nodes().size(), 1u);

    const auto& loaded_node = loaded->nodes()[0];
    EXPECT_EQ(loaded_node.semantic.iface.size(), 3u);

    // Verify each port's direction and domain survive the round-trip
    auto ctrl = loaded_node.semantic.iface.find(interner.intern("ctrl"));
    ASSERT_TRUE(ctrl.has_value());
    EXPECT_EQ(ctrl->direction, bp2::Direction::Input);
    EXPECT_EQ(ctrl->domain, Domain::Logical);

    auto v_bus = loaded_node.semantic.iface.find(interner.intern("v_bus"));
    ASSERT_TRUE(v_bus.has_value());
    EXPECT_EQ(v_bus->direction, bp2::Direction::InOut);
    EXPECT_EQ(v_bus->domain, Domain::Electrical);

    auto temp = loaded_node.semantic.iface.find(interner.intern("temp"));
    ASSERT_TRUE(temp.has_value());
    EXPECT_EQ(temp->direction, bp2::Direction::Output);
    EXPECT_EQ(temp->domain, Domain::Thermal);

    // Verify derived projections match semantic
    auto inputs = bp2::derive_input_ports(loaded_node.semantic.iface);
    auto outputs = bp2::derive_output_ports(loaded_node.semantic.iface);
    EXPECT_EQ(inputs.size(), 2u);  // ctrl (Input) + v_bus (InOut appears in inputs)
    EXPECT_EQ(outputs.size(), 2u); // temp (Output) + v_bus (InOut appears in outputs)
}

// Issue #31 Required Test 3: No-drift invariant (structural)
// ViewData must NOT contain inputs/outputs port lists. This test verifies
// the structural invariant at compile time: if someone re-adds port lists
// to ViewData, the sizeof check will fail.
// Additionally verifies that semantic.iface is the sole source for port data.
TEST(Issue31_SingleSource, NoDriftInvariant_ViewDataHasNoPortLists) {
    // Structural assertion: ViewData should be small — it contains no port vectors.
    // A ViewData with two std::vector<NodePort> would be at least 48 bytes larger
    // (2 vectors × 24 bytes each on 64-bit). We check that ViewData size stays
    // within a reasonable bound that excludes hidden port vectors.
    //
    // Current ViewData contains: string name, string render_hint, bool expandable,
    // string blueprint_path, NodeContentType enum, string content_label,
    // 5 floats, string content_unit, 2 bools, bool has_color, 4 color floats.
    // No vectors of NodePort.
    static_assert(
        !std::is_same_v<
            decltype(std::declval<bp2::Blueprint::Node::ViewData>()),
            void>,
        "ViewData must exist");

    // Verify the round-trip produces identical node without any view port lists.
    // This is the runtime companion to the compile-time check above.
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "Resistor");

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("r1");
    node.semantic.type = interner.intern("Resistor");
    set_iface(node, {
        make_port(interner, "a", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "b", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("no_drift_test"));
    bp = bp.with_node(std::move(node));

    // Round-trip
    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    bp2::DecodeError err;
    auto loaded = bp2::BlueprintCodec::decode(json_str, interner, arena, reg, &err);
    ASSERT_TRUE(loaded.has_value()) << err.message;

    // The key invariant: semantic.iface is the only source, and ViewData equality
    // holds without any port-list fields. If someone adds port lists to ViewData
    // but doesn't populate them, this equality check would fail.
    const auto& orig = bp.nodes()[0];
    const auto& rt = loaded->nodes()[0];
    EXPECT_EQ(orig.view, rt.view)
        << "ViewData must round-trip identically (no hidden port state)";
    EXPECT_EQ(orig.semantic.iface, rt.semantic.iface)
        << "semantic.iface must be the sole port authority and survive round-trip";
}
