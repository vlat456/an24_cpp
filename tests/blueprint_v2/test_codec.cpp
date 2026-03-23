#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include <nlohmann/json.hpp>

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
    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    ASSERT_TRUE(result.has_value()) << err.message;
    EXPECT_EQ(interner.resolve(result->id()), "test_bp");
    EXPECT_EQ(result->display_name(), "Test");
    EXPECT_TRUE(result->nodes().empty());
}

TEST(BlueprintCodec, DecodeNodesWithParams) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
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

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->nodes().size(), 1u);
    auto& node = result->nodes()[0];
    EXPECT_EQ(interner.resolve(node.id), "bat1");
    EXPECT_EQ(interner.resolve(node.type), "Battery");
    EXPECT_FLOAT_EQ(node.x, 100.0f);
    EXPECT_FLOAT_EQ(node.params.at(interner.intern("v_nominal")), 28.0f);
}

TEST(BlueprintCodec, DecodeWires) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [
            {"id": "w1", "source": "/bat1:v_out", "target": "/r1:in"}
        ],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->wires().size(), 1u);
    auto& wire = result->wires()[0];
    EXPECT_EQ(interner.resolve(wire.id), "w1");
    EXPECT_EQ(wire.source.kind(), bp2::PathKind::Port);
}
TEST(BlueprintCodec, RoundTripSimpleBlueprint) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("rt_test"))
           .with_display_name("Round-Trip Test");

    bp2::Blueprint::Node bat;
    bat.id = interner.intern("bat1");
    bat.type = interner.intern("Battery");
    bat.x = 10.0f;
    bat.y = 20.0f;
    bat.params[interner.intern("v_nominal")] = 28.0f;
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
    std::string json1 = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto decoded = bp2::BlueprintCodec::decode(json1, interner, arena, reg);
    ASSERT_TRUE(decoded.has_value());
    std::string json2 = bp2::BlueprintCodec::encode(*decoded, interner, arena);
    auto j1 = nlohmann::json::parse(json1);
    auto j2 = nlohmann::json::parse(json2);
    EXPECT_EQ(j2["version"], "3.0");
    EXPECT_EQ(j2["id"], j1["id"]);
    EXPECT_EQ(j2["display_name"], j1["display_name"]);
    ASSERT_EQ(j2["nodes"].size(), j1["nodes"].size());
    ASSERT_EQ(j2["wires"].size(), j1["wires"].size());
    EXPECT_EQ(j2["wires"][0]["source"], j1["wires"][0]["source"]);
    EXPECT_EQ(j2["wires"][0]["target"], j1["wires"][0]["target"]);
}

TEST(BlueprintCodec, DecodeDoesNotInferMissingNodePortsOrName) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(interner);

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "bat1",
                "type": "Battery",
                "position": {"x": 10.0, "y": 20.0}
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->nodes().size(), 1u);
    const auto& node = result->nodes()[0];
    EXPECT_TRUE(node.name.empty());
    EXPECT_TRUE(node.inputs.empty());
    EXPECT_TRUE(node.outputs.empty());
}

TEST(BlueprintCodec, DecodeBackfillsMissingParamsFromRegistryDefaults) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    reg.register_component(
        interner.intern("LUT"),
        bp2::Interface({
            {interner.intern("input"), Domain::Logical, bp2::Direction::Input},
            {interner.intern("output"), Domain::Logical, bp2::Direction::Output},
        }),
        "LUT"
    );
    auto* lut_entry = const_cast<bp2::TypeRegistry::Entry*>(reg.find(interner.intern("LUT")));
    ASSERT_NE(lut_entry, nullptr);
    lut_entry->param_defaults["table"] = "0:0; 100:100";
    lut_entry->param_defaults["gain"] = "1.5";

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "lut1",
                "type": "LUT",
                "position": {"x": 10.0, "y": 20.0}
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->nodes().size(), 1u);
    const auto& node = result->nodes()[0];

    auto gain_it = node.params.find(interner.intern("gain"));
    ASSERT_NE(gain_it, node.params.end());
    EXPECT_FLOAT_EQ(gain_it->second, 1.5f);

    auto table_it = node.string_params.find("table");
    ASSERT_NE(table_it, node.string_params.end());
    EXPECT_EQ(table_it->second, "0:0; 100:100");
}

TEST(BlueprintCodec, EncodeDecodePreservesBlueprintNameField) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("name_test"));
    bp = bp.with_display_name("Name Test");
    bp = bp.with_name("MyBlueprintName");

    std::string encoded = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto decoded = bp2::BlueprintCodec::decode(encoded, interner, arena, reg);
    ASSERT_TRUE(decoded.has_value());

    EXPECT_EQ(decoded->name(), "MyBlueprintName");
    EXPECT_EQ(decoded->display_name(), "Name Test");
}
TEST(BlueprintCodec, DecodeInvalidJsonReturnsNullopt) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;
    auto result = bp2::BlueprintCodec::decode("not json", interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(err.message.empty());
}

TEST(BlueprintCodec, DecodeMissingIdFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;

    std::string json = R"({"version": "3.0", "display_name": "X", "interface": [], "nodes": [], "wires": [], "nested": []})";
    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg);
    EXPECT_FALSE(result.has_value());
}

TEST(BlueprintCodec, DecodeMissingDisplayNameFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({"version": "3.0", "id": "x", "interface": [], "nodes": [], "wires": [], "nested": []})";
    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("display_name"), std::string::npos);
}

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
    auto decoded = bp2::BlueprintCodec::decode(json_str, interner, arena, reg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->iface().size(), 3u);
    auto ports = decoded->iface().ports();
    EXPECT_EQ(interner.resolve(ports[0].name), "v_in");
    EXPECT_EQ(ports[0].domain, Domain::Electrical);
    EXPECT_EQ(ports[0].direction, bp2::Direction::Input);
}

TEST(BlueprintCodec, EncodeDeterministicNodeAndWireOrdering) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("order_test"));
    bp = bp.with_display_name("Order Test");

    bp2::Blueprint::Node n3;
    n3.id = interner.intern("z_node");
    n3.type = interner.intern("Battery");
    bp = bp.with_node(std::move(n3));

    bp2::Blueprint::Node n1;
    n1.id = interner.intern("a_node");
    n1.type = interner.intern("Resistor");
    bp = bp.with_node(std::move(n1));

    bp2::Blueprint::Node n2;
    n2.id = interner.intern("m_node");
    n2.type = interner.intern("Switch");
    bp = bp.with_node(std::move(n2));

    bp2::Blueprint::Wire w2;
    w2.id = interner.intern("wire_20");
    w2.source = arena.make_port(arena.make_node(arena.root(), interner.intern("z_node")), interner.intern("v_out"));
    w2.target = arena.make_port(arena.make_node(arena.root(), interner.intern("a_node")), interner.intern("in"));
    bp = bp.with_wire(std::move(w2));

    bp2::Blueprint::Wire w1;
    w1.id = interner.intern("wire_10");
    w1.source = arena.make_port(arena.make_node(arena.root(), interner.intern("m_node")), interner.intern("v_out"));
    w1.target = arena.make_port(arena.make_node(arena.root(), interner.intern("a_node")), interner.intern("in"));
    bp = bp.with_wire(std::move(w1));

    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto j = nlohmann::json::parse(json_str);

    ASSERT_EQ(j["nodes"].size(), 3u);
    EXPECT_EQ(j["nodes"][0]["id"], "a_node");
    EXPECT_EQ(j["nodes"][1]["id"], "m_node");
    EXPECT_EQ(j["nodes"][2]["id"], "z_node");

    ASSERT_EQ(j["wires"].size(), 2u);
    EXPECT_EQ(j["wires"][0]["id"], "wire_10");
    EXPECT_EQ(j["wires"][1]["id"], "wire_20");
}

TEST(BlueprintCodec, EncodeDeterministicParamKeyOrdering) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("param_order_test"));
    bp = bp.with_display_name("Param Order Test");

    bp2::Blueprint::Node n;
    n.id = interner.intern("n1");
    n.type = interner.intern("Battery");
    n.params[interner.intern("zeta")] = 1.0f;
    n.params[interner.intern("alpha")] = 2.0f;
    n.string_params["gamma"] = "x";
    n.string_params["beta"] = "y";
    bp = bp.with_node(std::move(n));

    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto j = nlohmann::json::parse(json_str);
    std::string dumped = j["nodes"][0]["params"].dump();
    std::string dumped_s = j["nodes"][0]["string_params"].dump();

    EXPECT_LT(dumped.find("\"alpha\""), dumped.find("\"zeta\""));
    EXPECT_LT(dumped_s.find("\"beta\""), dumped_s.find("\"gamma\""));
}
