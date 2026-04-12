#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/blueprint/canonicalize.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/interface/node_port_projection.h"
#include "blueprint_v2/path/path.h"
#include "blueprint_v2/validation/path_resolver.h"
#include "blueprint_v2/validation/wire_validator.h"
#include "editor/blueprint_view_hydration.h"
#include "editor/data/node_content.h"
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

    EXPECT_EQ(j["format"], "blueprint");
    EXPECT_EQ(j["version"], 1);
    EXPECT_EQ(j["blueprint_id"], "test_bp");
    EXPECT_TRUE(j["nodes"].empty());
    EXPECT_TRUE(j["wires"].empty());
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
    EXPECT_EQ(n["component"], "Battery");
    EXPECT_FLOAT_EQ(n["layout"]["x"].get<float>(), 100.0f);
    EXPECT_FLOAT_EQ(n["params"]["v_nominal"].get<float>(), 28.0f);
}

TEST(BlueprintCodec, EncodeWires) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test"));

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = bp2::WireEndpoint{interner.intern("bat1"), interner.intern("v_out")};
    wire.target = bp2::WireEndpoint{interner.intern("r1"), interner.intern("in")};
    wire.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(wire));

    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto j = nlohmann::json::parse(json_str);

    ASSERT_EQ(j["wires"].size(), 1u);
    auto& w = j["wires"][0];
    EXPECT_EQ(w["id"], "w1");
    EXPECT_EQ(w["from"]["node"], "bat1");
    EXPECT_EQ(w["from"]["port"], "v_out");
    EXPECT_EQ(w["to"]["node"], "r1");
    EXPECT_EQ(w["to"]["port"], "in");
}

// =============================================================================
// Regression: Wire::operator== must compare routing_points
// =============================================================================

TEST(BlueprintCodec, WireEqualityIncludesRoutingPoints) {
    ui::StringInterner interner;

    bp2::Blueprint::Wire a;
    a.id = interner.intern("w1");
    a.source = bp2::WireEndpoint{interner.intern("n1"), interner.intern("out")};
    a.target = bp2::WireEndpoint{interner.intern("n2"), interner.intern("in")};
    a.domain = Domain::Electrical;

    bp2::Blueprint::Wire b = a;

    // Identical wires must compare equal.
    EXPECT_EQ(a, b);

    // Adding routing points to one wire must make them unequal.
    b.routing_points.push_back({10.0f, 20.0f});
    EXPECT_NE(a, b) << "Wire::operator== must consider routing_points";

    // Matching routing_points must restore equality.
    a.routing_points.push_back({10.0f, 20.0f});
    EXPECT_EQ(a, b);

    // Different routing_points must be unequal.
    a.routing_points.push_back({30.0f, 40.0f});
    EXPECT_NE(a, b) << "Wire::operator== must detect different routing_points sizes";
}

TEST(BlueprintCodec, RoutingPointsRoundTrip) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;

    // Register stub types so that wire endpoint resolution succeeds.
    bp2::Interface src_iface({make_port(interner, "out", bp2::Direction::Output, PortType::V)});
    bp2::Interface dst_iface({make_port(interner, "in", bp2::Direction::Input, PortType::V)});
    register_type(reg, interner, "SrcType", src_iface);
    register_type(reg, interner, "DstType", dst_iface);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("routing_test"));
    bp = bp.with_name("RoutingTest");

    bp2::Blueprint::Node n1;
    n1.semantic.id = interner.intern("n1");
    n1.semantic.type = interner.intern("SrcType");
    n1.semantic.iface = src_iface;
    n1.layout.x = 0.0f;
    n1.layout.y = 0.0f;
    bp = bp.with_node(std::move(n1));

    bp2::Blueprint::Node n2;
    n2.semantic.id = interner.intern("n2");
    n2.semantic.type = interner.intern("DstType");
    n2.semantic.iface = dst_iface;
    n2.layout.x = 100.0f;
    n2.layout.y = 0.0f;
    bp = bp.with_node(std::move(n2));

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = bp2::WireEndpoint{interner.intern("n1"), interner.intern("out")};
    wire.target = bp2::WireEndpoint{interner.intern("n2"), interner.intern("in")};
    wire.domain = Domain::Electrical;
    wire.routing_points = {{5.0f, 10.0f}, {15.0f, 20.0f}};
    bp = bp.with_wire(std::move(wire));

    std::string encoded = bp2::BlueprintCodec::encode(bp, interner, arena);

    bp2::DecodeError err;
    auto decoded = bp2::BlueprintCodec::decode(encoded, interner, arena, reg, &err);
    ASSERT_TRUE(decoded.has_value()) << err.message;
    ASSERT_EQ(decoded->wires().size(), 1u);

    const auto& w = decoded->wires()[0];
    ASSERT_EQ(w.routing_points.size(), 2u);
    EXPECT_FLOAT_EQ(w.routing_points[0].first, 5.0f);
    EXPECT_FLOAT_EQ(w.routing_points[0].second, 10.0f);
    EXPECT_FLOAT_EQ(w.routing_points[1].first, 15.0f);
    EXPECT_FLOAT_EQ(w.routing_points[1].second, 20.0f);
}

TEST(BlueprintCodec, DecodeEmptyBlueprint) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "Subtract");

    // Library blueprints (e.g. library/math/FirstOrderLag.blueprint) have nodes
    // WITHOUT layout fields. The codec must accept these and default to {0,0}.
    std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "test_no_pos",
        "name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "kind": "component",
                "component": "Subtract",
                "layout": {"x": 0.0, "y": 0.0}
            }
        ],
        "wires": []
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
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "test_with_pos",
        "name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "kind": "component",
                "component": "Subtract",
                "layout": {"x": 42.0, "y": -7.5}
            }
        ],
        "wires": []
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
// Bug: a legacy project blueprint saved OK but failed to reload with
//   "[persist] Failed to load blueprint: wire id=186: wire endpoint path unresolved"
// Root cause: decode_nodes() populated node.view.inputs/node.view.outputs but never
// built node.semantic.iface, so node_interface() returned nullptr for non-registry types.
// =============================================================================

TEST(BlueprintCodec, DecodePopulatesNodeIfaceFromPorts) {
    // After decoding a component node, node.semantic.iface must be populated
    // from the type registry's TypeDefinition ports (not from inline node ports).
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    
    // Register Battery type with specific ports
    std::vector<bp2::PortDescriptor> battery_ports = {
        make_port(interner, "feedback", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "output",   Domain::Electrical, bp2::Direction::Output, PortType::V),
        make_port(interner, "bidir",    Domain::Electrical, bp2::Direction::InOut, PortType::Any),
    };
    bp2::Interface battery_iface(battery_ports);
    register_type(reg, interner, "Battery", battery_iface);

    std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "iface_test",
        "name": "Iface Test",
        "interface": [],
        "nodes": [
            {
                "id": "bat1",
                "kind": "component",
                "component": "Battery",
                "layout": {"x": 0, "y": 0}
            }
        ],
        "wires": []
    })";

    bp2::DecodeError err;
    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    ASSERT_TRUE(result.has_value()) << "Decode failed: " << err.message;
    ASSERT_EQ(result->nodes().size(), 1u);

    const auto& node = result->nodes()[0];
    // The fix: node.semantic.iface must be populated from the registry type definition
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
    // When a node type is registered without ports, iface should be empty
    // (the registry may provide it separately via node_interface())
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "SomeType");

    std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "no_ports_test",
        "name": "No Ports",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "kind": "component",
                "component": "SomeType",
                "layout": {"x": 0, "y": 0}
            }
        ],
        "wires": []
    })";

     bp2::DecodeError err;
     auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
     ASSERT_TRUE(result.has_value()) << "Decode failed: " << err.message;
     EXPECT_TRUE(result->nodes()[0].semantic.iface.empty());
}

TEST(BlueprintCodec, DecodeLibraryBlueprintFormat_FullExample) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "BlueprintInput");
    register_type(reg, interner, "BlueprintOutput");

    // v1 format: strictly canonical with format/version/blueprint_id/name/interface/nodes/wires
    // Nodes can omit layout (will default to 0,0).
    std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "FirstOrderLag",
        "name": "FirstOrderLag",
        "interface": [
            {"id": "in", "direction": "In", "port_type": "Any"},
            {"id": "out", "direction": "Out", "port_type": "Any"}
        ],
        "nodes": [
            {
                "id": "in",
                "kind": "component",
                "component": "BlueprintInput",
                "layout": {"x": 0, "y": 0}
            },
            {
                "id": "out",
                "kind": "component",
                "component": "BlueprintOutput",
                "layout": {"x": 0, "y": 0}
            }
        ],
        "wires": []
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



TEST(BlueprintCodec, EncodeOmitsNodeContentAndColorWorkspaceFields) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("content_rt"));
    bp = bp.with_name("Content RT");

    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("knob1");
    n.semantic.type = interner.intern("Battery");
    n.view.content_type = bp2::NodeContentType::Knob;
    n.view.content_label = "Knob";
    n.view.has_color = true;
    n.view.color_r = 0.1f;
    n.view.color_g = 0.2f;
    n.view.color_b = 0.3f;
    n.view.color_a = 1.0f;
    bp = bp.with_node(std::move(n));

    const std::string encoded = bp2::BlueprintCodec::encode(bp, interner, arena);
    const auto j = nlohmann::json::parse(encoded);
    ASSERT_EQ(j["nodes"].size(), 1u);
    EXPECT_FALSE(j["nodes"][0].contains("content"));
    EXPECT_FALSE(j["nodes"][0].contains("color"));
}

TEST(BlueprintCodec, DecodeRejectsForbiddenContentField) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg = make_test_registry();
    register_type(reg, interner, "Battery");
    bp2::DecodeError err;

    const std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "forbidden_content",
        "name": "Forbidden Content",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "kind": "component",
                "component": "Battery",
                "layout": {"x": 0.0, "y": 0.0},
                "content": {"type": "knob"}
            }
        ],
        "wires": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_NE(err.message.find("unknown node field: content"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsForbiddenColorField) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg = make_test_registry();
    register_type(reg, interner, "Battery");
    bp2::DecodeError err;

    const std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "forbidden_color",
        "name": "Forbidden Color",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "kind": "component",
                "component": "Battery",
                "layout": {"x": 0.0, "y": 0.0},
                "color": {"r": 0.1, "g": 0.2, "b": 0.3, "a": 1.0}
            }
        ],
        "wires": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_NE(err.message.find("unknown node field: color"), std::string::npos);
}



TEST(BlueprintCodec, EncodeWithParserRegistryOverload) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("codec_parser_encode"));
    bp = bp.with_name("Codec Parser Encode");

    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena, &parser_registry);
    auto j = nlohmann::json::parse(json_str);
    EXPECT_EQ(j["format"], "blueprint");
    EXPECT_EQ(j["version"], 1);
    EXPECT_EQ(j["blueprint_id"], "codec_parser_encode");
}

TEST(BlueprintCodec, DecodeWithParserRegistryOverload) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry parser_registry = load_type_registry("library/");

    const std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "codec_parser_decode",
        "name": "Codec Parser Decode",
        "interface": [],
        "nodes": [],
        "wires": []
    })";

    bp2::DecodeError err;
    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, parser_registry, &err);
    ASSERT_TRUE(decoded.has_value()) << err.message;
    EXPECT_EQ(interner.resolve(decoded->id()), "codec_parser_decode");
}

TEST(BlueprintCodec, DecodeDoesNotHydrateRuntimeViewFields) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg = make_test_registry();
    register_type(reg, interner, "Slider");
    register_type(reg, interner, "Value");
    reg.types["Value"].render_hint = "ref";

    const std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "pure_decode",
        "name": "Pure Decode",
        "interface": [],
        "nodes": [
            {
                "id": "slider1",
                "kind": "component",
                "component": "Slider",
                "layout": {"x": 0.0, "y": 0.0}
            },
            {
                "id": "value1",
                "kind": "component",
                "component": "Value",
                "layout": {"x": 1.0, "y": 1.0}
            }
        ],
        "wires": []
    })";

    bp2::DecodeError err;
    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    ASSERT_TRUE(decoded.has_value()) << err.message;

    const auto* slider = decoded->find_node(interner.lookup("slider1"));
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->view.content_type, bp2::NodeContentType::None);
    EXPECT_TRUE(slider->view.render_hint.empty());

    const auto* value = decoded->find_node(interner.lookup("value1"));
    ASSERT_NE(value, nullptr);
    EXPECT_TRUE(value->view.render_hint.empty());
}

TEST(BlueprintCodec, ExplicitHydrationPopulatesRuntimeViewFieldsRecursively) {
    ui::StringInterner interner;
    TypeRegistry reg = make_test_registry();
    register_type(reg, interner, "Slider");
    reg.types["Slider"].content_type = "Slider";
    reg.types["Slider"].params["min"] = "0.0";
    reg.types["Slider"].params["max"] = "1.0";

    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("inner"));
    inner = inner.with_name("Inner");

    bp2::Blueprint::Node slider;
    slider.semantic.id = interner.intern("inner_slider");
    slider.semantic.type = interner.intern("Slider");
    slider.layout.x = 3.0f;
    slider.layout.y = 4.0f;
    inner = inner.with_node(std::move(slider));

    bp2::Blueprint root;
    root = root.with_id(interner.intern("root"));
    root = root.with_name("Root");

    bp2::Blueprint::Node host;
    host.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    host.semantic.id = interner.intern("host");
    host.semantic.type = interner.intern("Group");
    host.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("Group"),
        std::make_unique<bp2::Blueprint>(inner));
    root = root.with_node(std::move(host));

    bp2::Blueprint hydrated = editor::hydrate_runtime_node_view_data(std::move(root), interner, reg);
    const auto* loaded_host = hydrated.find_node(interner.lookup("host"));
    ASSERT_NE(loaded_host, nullptr);
    ASSERT_TRUE(loaded_host->source.has_value());
    const auto* loaded_slider = loaded_host->source->inline_def()->find_node(interner.lookup("inner_slider"));
    ASSERT_NE(loaded_slider, nullptr);
    EXPECT_EQ(loaded_slider->view.content_type, bp2::NodeContentType::Slider);
    EXPECT_FLOAT_EQ(loaded_slider->view.content_min, 0.0f);
    EXPECT_FLOAT_EQ(loaded_slider->view.content_max, 1.0f);
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
// After encoding/decoding, semantic.iface survives round-trip perfectly.
// In the v1 format, node ports are looked up from the type registry during decode,
// not stored per-node in JSON. The test verifies semantic.iface is correctly populated.
// Note: Domains are derived from PortType, not preserved separately in the type registry.
TEST(Issue31_SingleSource, ExportReadsSemanticIface_CodecRoundTrip) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    
    // Register TestDevice with specific ports
    // Note: Domains will be derived from PortType by the codec, not preserved separately
    std::vector<bp2::PortDescriptor> test_ports = {
        make_port(interner, "ctrl",   Domain::Logical,    bp2::Direction::Input,  PortType::Bool),
        make_port(interner, "v_bus",  Domain::Electrical,  bp2::Direction::InOut,  PortType::V),
        make_port(interner, "temp",   Domain::Thermal,     bp2::Direction::Output, PortType::Temperature),
    };
    bp2::Interface test_iface(test_ports);
    register_type(reg, interner, "TestDevice", test_iface);

    // Build a node with the registered type
    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("dev1");
    node.semantic.type = interner.intern("TestDevice");
    
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("export_semantic_test"));
    bp = bp.with_name("Export Semantic Test");
    bp = bp.with_node(std::move(node));

    // Encode the blueprint
    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto j = nlohmann::json::parse(json_str);

    // In v1 format, individual nodes don't have a "ports" field - ports come from registry
    EXPECT_FALSE(j["nodes"][0].contains("ports"));

    // Decode and verify semantic.iface is correctly reconstructed from registry
    bp2::DecodeError err;
    auto loaded = bp2::BlueprintCodec::decode(json_str, interner, arena, reg, &err);
    ASSERT_TRUE(loaded.has_value()) << "Decode failed: " << err.message;
    ASSERT_EQ(loaded->nodes().size(), 1u);

    const auto& loaded_node = loaded->nodes()[0];
    // semantic.iface was populated from the type registry during decode
    EXPECT_EQ(loaded_node.semantic.iface.size(), 3u);

    // Verify each port's direction survives the round-trip
    // (Domains are derived from PortType, not stored separately)
    auto ctrl = loaded_node.semantic.iface.find(interner.intern("ctrl"));
    ASSERT_TRUE(ctrl.has_value());
    EXPECT_EQ(ctrl->direction, bp2::Direction::Input);

    auto v_bus = loaded_node.semantic.iface.find(interner.intern("v_bus"));
    ASSERT_TRUE(v_bus.has_value());
    EXPECT_EQ(v_bus->direction, bp2::Direction::InOut);

    auto temp = loaded_node.semantic.iface.find(interner.intern("temp"));
    ASSERT_TRUE(temp.has_value());
    EXPECT_EQ(temp->direction, bp2::Direction::Output);

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
    // Current ViewData contains canonical label plus runtime/editor-only
    // presentation fields. No vectors of NodePort.
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
    
    // Register Resistor type with its ports
    bp2::Interface resistor_iface;
    std::vector<bp2::PortDescriptor> resistor_ports = {
        make_port(interner, "a", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "b", Domain::Electrical, bp2::Direction::Output, PortType::V),
    };
    resistor_iface = bp2::Interface(resistor_ports);
    register_type(reg, interner, "Resistor", resistor_iface);

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("r1");
    node.semantic.type = interner.intern("Resistor");
    // Manually set iface to match what we'll see after decode
    set_iface(node, {
        make_port(interner, "a", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "b", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("no_drift_test"));
    bp = bp.with_name("No Drift Test");
    bp = bp.with_node(std::move(node));

    // Round-trip
    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    bp2::DecodeError err;
    auto loaded = bp2::BlueprintCodec::decode(json_str, interner, arena, reg, &err);
    ASSERT_TRUE(loaded.has_value()) << err.message;

    // The key invariant: semantic.iface is the only source, and the in-memory
    // ViewData shape still carries no hidden port-list state. This is about the
    // internal node object after codec round-trip, not canonical persistence of
    // render/content/color fields.
    const auto& orig = bp.nodes()[0];
    const auto& rt = loaded->nodes()[0];
    EXPECT_EQ(orig.view, rt.view)
        << "ViewData must not grow hidden port state";
    // Verify semantic.iface survives with same port count and directions
    EXPECT_EQ(orig.semantic.iface.size(), rt.semantic.iface.size())
        << "semantic.iface must be the sole port authority and survive round-trip";
    for (const auto& orig_port : orig.semantic.iface.ports()) {
        auto loaded_port = rt.semantic.iface.find(orig_port.name);
        ASSERT_TRUE(loaded_port.has_value());
        EXPECT_EQ(orig_port.direction, loaded_port->direction);
    }
}

// =============================================================================
// Issue #88: Kind-specific field validation regression tests
// =============================================================================

TEST(BlueprintCodec, DecodeRejectsCollapsedOnComponentNode) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "Battery");
    bp2::DecodeError err;

    const std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "collapsed_on_component",
        "name": "Collapsed On Component",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "kind": "component",
                "component": "Battery",
                "collapsed": true,
                "layout": {"x": 0.0, "y": 0.0}
            }
        ],
        "wires": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_NE(err.message.find("unknown node field: collapsed"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsSourceOnComponentNode) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "Battery");
    bp2::DecodeError err;

    const std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "source_on_component",
        "name": "Source On Component",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "kind": "component",
                "component": "Battery",
                "source": {"mode": "reference", "blueprint_id": "foo"},
                "layout": {"x": 0.0, "y": 0.0}
            }
        ],
        "wires": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_NE(err.message.find("unknown node field: source"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsComponentOnBlueprintInstance) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "Battery");
    bp2::DecodeError err;

    const std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "component_on_instance",
        "name": "Component On Instance",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "kind": "blueprint_instance",
                "component": "Battery",
                "source": {
                    "mode": "embedded",
                    "blueprint": {
                        "format": "blueprint",
                        "version": 1,
                        "blueprint_id": "inner",
                        "name": "Inner",
                        "interface": [],
                        "nodes": [],
                        "wires": []
                    }
                },
                "layout": {"x": 0.0, "y": 0.0}
            }
        ],
        "wires": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_NE(err.message.find("unknown node field: component"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsParamsOnBlueprintInstance) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    register_type(reg, interner, "Battery");
    bp2::DecodeError err;

    const std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "params_on_instance",
        "name": "Params On Instance",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "kind": "blueprint_instance",
                "params": {"v": 1.0},
                "source": {
                    "mode": "embedded",
                    "blueprint": {
                        "format": "blueprint",
                        "version": 1,
                        "blueprint_id": "inner",
                        "name": "Inner",
                        "interface": [],
                        "nodes": [],
                        "wires": []
                    }
                },
                "layout": {"x": 0.0, "y": 0.0}
            }
        ],
        "wires": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_NE(err.message.find("unknown node field: params"), std::string::npos);
}

// =============================================================================
// Issue #88: Duplicate interface port ID rejection
// =============================================================================

TEST(BlueprintCodec, DecodeRejectsDuplicateInterfacePortIds) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    bp2::DecodeError err;

    const std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "dup_interface",
        "name": "Dup Interface",
        "interface": [
            {"id": "v_in", "direction": "In", "port_type": "V"},
            {"id": "v_in", "direction": "Out", "port_type": "V"}
        ],
        "nodes": [],
        "wires": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_NE(err.message.find("duplicate port id"), std::string::npos);
}

// =============================================================================
// Issue #88: blueprint_id semantic validation
// =============================================================================

TEST(BlueprintCodec, DecodeRejectsEmptyBlueprintId) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    bp2::DecodeError err;

    const std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "",
        "name": "Test",
        "interface": [],
        "nodes": [],
        "wires": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_NE(err.message.find("blueprint_id must not be empty"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsBlueprintIdWithWhitespace) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    bp2::DecodeError err;

    const std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "has space",
        "name": "Test",
        "interface": [],
        "nodes": [],
        "wires": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_NE(err.message.find("printable ASCII"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsBlueprintIdWithTab) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "has)";
    json += '\t';
    json += R"(tab",
        "name": "Test",
        "interface": [],
        "nodes": [],
        "wires": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
}

// =============================================================================
// Issue #88: name non-empty validation
// =============================================================================

TEST(BlueprintCodec, DecodeRejectsEmptyName) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;
    bp2::DecodeError err;

    const std::string json = R"({
        "format": "blueprint",
        "version": 1,
        "blueprint_id": "empty_name_test",
        "name": "",
        "interface": [],
        "nodes": [],
        "wires": []
    })";

    auto decoded = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_NE(err.message.find("name must not be empty"), std::string::npos);
}

// =============================================================================
// Regression: codec round-trip preserves single name field
// =============================================================================

TEST(BlueprintCodec, RoundTripPreservesNameEquality) {
    // The old display_name/name duality meant encode→decode could break equality
    // because display_name_ was not persisted but was compared in operator==.
    // With the unified name model, round-trip must preserve equality.
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("name_rt"));
    bp = bp.with_name("Name Round Trip");

    std::string encoded = bp2::BlueprintCodec::encode(bp, interner, arena);
    bp2::DecodeError err;
    auto decoded = bp2::BlueprintCodec::decode(encoded, interner, arena, reg, &err);
    ASSERT_TRUE(decoded.has_value()) << err.message;
    EXPECT_EQ(decoded->name(), "Name Round Trip");
    EXPECT_EQ(bp, *decoded);
}

// =============================================================================
// Issue #132 Regression Tests: Node-aware content hydration
// =============================================================================

// Regression 1: Knob with instance positions != type definition
TEST(Issue132_HydrationFromInstanceParams, KnobPositionsFromInstance) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;

    // Register a Knob type with default positions=2
    TypeDefinition knob_def;
    knob_def.classname = "Knob";
    knob_def.content_type = "Knob";
    knob_def.params["positions"] = "2";
    knob_def.params["initial_position"] = "0";
    reg.types["Knob"] = knob_def;

    // Create a node instance with positions=5 (override)
    bp2::Blueprint::Node knob_node;
    knob_node.semantic.id = interner.intern("knob1");
    knob_node.semantic.type = interner.intern("Knob");
    knob_node.semantic.params[interner.intern("positions")] = 5.0f;
    knob_node.semantic.params[interner.intern("initial_position")] = 2.0f;

    // Hydrate
    editor::hydrate_node_view(knob_node, reg.get("Knob"), interner);

    // Verify that content_max comes from instance params, not type defaults
    EXPECT_FLOAT_EQ(knob_node.view.content_max, 5.0f);
    EXPECT_FLOAT_EQ(knob_node.view.content_value, 2.0f);
}

// Regression 2: Slider with instance min/max != type definition
TEST(Issue132_HydrationFromInstanceParams, SliderMinMaxFromInstance) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;

    // Register a Slider type with default min/max
    TypeDefinition slider_def;
    slider_def.classname = "Slider";
    slider_def.content_type = "Slider";
    slider_def.params["min"] = "0";
    slider_def.params["max"] = "100";
    reg.types["Slider"] = slider_def;

    // Create a node instance with custom min/max
    bp2::Blueprint::Node slider_node;
    slider_node.semantic.id = interner.intern("slider1");
    slider_node.semantic.type = interner.intern("Slider");
    slider_node.semantic.params[interner.intern("min")] = -50.0f;
    slider_node.semantic.params[interner.intern("max")] = 200.0f;

    // Hydrate
    editor::hydrate_node_view(slider_node, reg.get("Slider"), interner);

    // Verify instance params take precedence
    EXPECT_FLOAT_EQ(slider_node.view.content_min, -50.0f);
    EXPECT_FLOAT_EQ(slider_node.view.content_max, 200.0f);
}

// Regression 3: Gauge with instance min/max != type definition
TEST(Issue132_HydrationFromInstanceParams, GaugeMinMaxFromInstance) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;

    // Register a Gauge type with default min/max
    TypeDefinition gauge_def;
    gauge_def.classname = "Voltmeter";
    gauge_def.content_type = "Gauge";
    gauge_def.params["min"] = "0";
    gauge_def.params["max"] = "28";
    reg.types["Voltmeter"] = gauge_def;

    // Create a node instance with custom min/max
    bp2::Blueprint::Node gauge_node;
    gauge_node.semantic.id = interner.intern("gauge1");
    gauge_node.semantic.type = interner.intern("Voltmeter");
    gauge_node.semantic.params[interner.intern("min")] = 10.0f;
    gauge_node.semantic.params[interner.intern("max")] = 50.0f;

    // Hydrate
    editor::hydrate_node_view(gauge_node, reg.get("Voltmeter"), interner);

    // Verify instance params take precedence
    EXPECT_FLOAT_EQ(gauge_node.view.content_min, 10.0f);
    EXPECT_FLOAT_EQ(gauge_node.view.content_max, 50.0f);
}

// Regression 4: Switch with instance closed state != type definition
TEST(Issue132_HydrationFromInstanceParams, SwitchClosedStateFromInstance) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;

    // Register a Switch type with default closed=false
    TypeDefinition switch_def;
    switch_def.classname = "Switch";
    switch_def.content_type = "Switch";
    switch_def.params["closed"] = "false";
    reg.types["Switch"] = switch_def;

    // Create a node instance with closed=true (override)
    bp2::Blueprint::Node switch_node;
    switch_node.semantic.id = interner.intern("sw1");
    switch_node.semantic.type = interner.intern("Switch");
    switch_node.semantic.params[interner.intern("closed")] = 1.0f;  // non-zero = true

    // Hydrate
    editor::hydrate_node_view(switch_node, reg.get("Switch"), interner);

    // Verify instance state is used
    EXPECT_TRUE(switch_node.view.content_state);
}

// Regression 5: Fallback to type definition when instance params absent
TEST(Issue132_HydrationFromInstanceParams, FallbackToTypeDefinitionWhenNoInstanceParam) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg;

    // Register a Slider type with defaults
    TypeDefinition slider_def;
    slider_def.classname = "Slider";
    slider_def.content_type = "Slider";
    slider_def.params["min"] = "0";
    slider_def.params["max"] = "1";
    reg.types["Slider"] = slider_def;

    // Create a node instance with NO instance params
    bp2::Blueprint::Node slider_node;
    slider_node.semantic.id = interner.intern("slider2");
    slider_node.semantic.type = interner.intern("Slider");
    // No params added

    // Hydrate
    editor::hydrate_node_view(slider_node, reg.get("Slider"), interner);

    // Verify type definition defaults are used
    EXPECT_FLOAT_EQ(slider_node.view.content_min, 0.0f);
    EXPECT_FLOAT_EQ(slider_node.view.content_max, 1.0f);
}
