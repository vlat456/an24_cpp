#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "blueprint_v2/validation/path_resolver.h"
#include "blueprint_v2/validation/wire_validator.h"
#include "json_parser/json_parser.h"
#include <nlohmann/json.hpp>

// ==============================================================================
// Helper: register a lightweight type stub in the parser TypeRegistry
// ==============================================================================
namespace {

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
    node.id = interner.intern("bat1");
    node.type = interner.intern("Battery");
    node.x = 100.0f;
    node.y = 200.0f;
    node.params[interner.intern("v_nominal")] = 28.0f;
    node.params[interner.intern("capacity")] = 24.0f;
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

TEST(BlueprintCodec, EncodeNestedEmbedded) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("inner_bp"));
    bp2::Blueprint::Node inner_node;
    inner_node.id = interner.intern("r1");
    inner_node.type = interner.intern("Resistor");
    inner = inner.with_node(std::move(inner_node));

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
    EXPECT_FLOAT_EQ(result->nodes()[0].x, 0.0f);
    EXPECT_FLOAT_EQ(result->nodes()[0].y, 0.0f);
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
    EXPECT_FLOAT_EQ(result->nodes()[0].x, 42.0f);
    EXPECT_FLOAT_EQ(result->nodes()[0].y, -7.5f);
}

// =============================================================================
// Regression: node.iface must be populated from decoded ports so that
// PathResolver can resolve wire endpoints even when the node type is NOT
// in the library registry (e.g. embedded blueprint proxy nodes).
// Bug: closed_circuit.blueprint saved OK but failed to reload with
//   "[persist] Failed to load blueprint: wire id=186: wire endpoint path unresolved"
// Root cause: decode_nodes() populated node.inputs/node.outputs but never
// built node.iface, so node_interface() returned nullptr for non-registry types.
// =============================================================================

TEST(BlueprintCodec, DecodePopulatesNodeIfaceFromPorts) {
    // After decoding a node with "ports", node.iface must be non-empty
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
    // The fix: node.iface must be populated from the decoded ports
    EXPECT_FALSE(node.iface.empty());
    EXPECT_EQ(node.iface.size(), 3u);

    // Verify each port is findable by name
    EXPECT_TRUE(node.iface.has(interner.intern("feedback")));
    EXPECT_TRUE(node.iface.has(interner.intern("output")));
    EXPECT_TRUE(node.iface.has(interner.intern("bidir")));

    // Verify directions
    auto fb = node.iface.find(interner.intern("feedback"));
    ASSERT_TRUE(fb.has_value());
    EXPECT_EQ(fb->direction, bp2::Direction::Input);

    auto out = node.iface.find(interner.intern("output"));
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->direction, bp2::Direction::Output);

    auto bd = node.iface.find(interner.intern("bidir"));
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
    EXPECT_TRUE(result->nodes()[0].iface.empty());
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
    bat.id = interner.intern("bat1");
    bat.type = interner.intern("Battery");
    bat.x = 10.0f;
    bat.y = 20.0f;
    bat.outputs.emplace_back(interner.intern("v_out"), PortSide::Output, PortType::V);
    bp = bp.with_node(std::move(bat));

    // Proxy node (type NOT in registry, expandable with embedded nested def)
    bp2::Blueprint::Node proxy;
    proxy.id = interner.intern("extract_inst_1");
    proxy.type = interner.intern("RN-180-Exciter");
    proxy.expandable = true;
    proxy.x = 100.0f;
    proxy.y = 200.0f;
    proxy.inputs.emplace_back(interner.intern("feedback"), PortSide::Input, PortType::V);
    proxy.outputs.emplace_back(interner.intern("output"), PortSide::Output, PortType::V);
    proxy.iface = bp2::Interface({
        {interner.intern("feedback"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("output"), Domain::Electrical, bp2::Direction::Output},
    });
    bp = bp.with_node(std::move(proxy));

    // Matching embedded nested definition (required by InvariantChecker for
    // expandable proxy nodes with unknown type)
    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("RN-180-Exciter"));
    inner = inner.with_display_name("RN-180 Exciter");
    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("extract_inst_1"); // same as proxy node id
    nested.blueprint_id = interner.intern("RN-180-Exciter");
    nested.embedded = true;
    nested.inline_def = std::make_unique<bp2::Blueprint>(inner);
    nested.x = 100.0f;
    nested.y = 200.0f;
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
                    "interface": [],
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
        EXPECT_FLOAT_EQ(node.x, 0.0f);
        EXPECT_FLOAT_EQ(node.y, 0.0f);
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
                        {"name": "in_port",  "domain": 1, "direction": 0},
                        {"name": "out_port", "domain": 1, "direction": 1}
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
    EXPECT_TRUE(nested.embedded);
    ASSERT_NE(nested.inline_def, nullptr);

    // THE REGRESSION: nested.iface must mirror inline_def's interface
    EXPECT_FALSE(nested.iface.empty())
        << "nested.iface is empty after decode — inline_def iface not propagated";
    EXPECT_EQ(nested.iface.size(), 2u);
    EXPECT_TRUE(nested.iface.has(interner.intern("in_port")));
    EXPECT_TRUE(nested.iface.has(interner.intern("out_port")));

    // Verify directions match the inline definition
    auto in_port = nested.iface.find(interner.intern("in_port"));
    ASSERT_TRUE(in_port.has_value());
    EXPECT_EQ(in_port->direction, bp2::Direction::Input);

    auto out_port = nested.iface.find(interner.intern("out_port"));
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

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("inst1");
    nested.blueprint_id = interner.intern("SubSystem");
    nested.embedded = true;
    nested.inline_def = std::make_unique<bp2::Blueprint>(inner);
    nested.iface = inner.iface();
    nested.x = 5.0f;
    nested.y = 10.0f;

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
    EXPECT_FALSE(loaded_nested.iface.empty())
        << "nested.iface lost during roundtrip";
    EXPECT_EQ(loaded_nested.iface.size(), 2u);
    EXPECT_TRUE(loaded_nested.iface.has(interner.intern("sig_in")));
    EXPECT_TRUE(loaded_nested.iface.has(interner.intern("sig_out")));
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
    n.id = interner.intern("knob1");
    n.type = interner.intern("Battery");
    n.content_type = bp2::NodeContentType::Knob;
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
    EXPECT_EQ(decoded->nodes()[0].content_type, bp2::NodeContentType::Knob);
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
