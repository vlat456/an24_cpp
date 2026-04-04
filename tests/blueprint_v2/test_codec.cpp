#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "blueprint_v2/validation/path_resolver.h"
#include "blueprint_v2/validation/wire_validator.h"
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
    bp2::TypeRegistry reg = bp2::TypeRegistry::create_test_registry(interner);

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {"id": "bat1", "type": "Battery", "position": {"x": 0.0, "y": 0.0}},
            {"id": "r1", "type": "Resistor", "position": {"x": 10.0, "y": 0.0}}
        ],
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

TEST(BlueprintCodec, EncodePreservesWireInsertionOrder) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("order_test"));

    bp2::Blueprint::Wire w2;
    w2.id = interner.intern("wire_2");
    w2.source = arena.make_port(arena.make_node(arena.root(), interner.intern("a")), interner.intern("o"));
    w2.target = arena.make_port(arena.make_node(arena.root(), interner.intern("b")), interner.intern("i"));
    bp = bp.with_wire(w2);

    bp2::Blueprint::Wire w10;
    w10.id = interner.intern("wire_10");
    w10.source = arena.make_port(arena.make_node(arena.root(), interner.intern("a")), interner.intern("o"));
    w10.target = arena.make_port(arena.make_node(arena.root(), interner.intern("c")), interner.intern("i"));
    bp = bp.with_wire(w10);

    bp2::Blueprint::Wire w1;
    w1.id = interner.intern("wire_1");
    w1.source = arena.make_port(arena.make_node(arena.root(), interner.intern("a")), interner.intern("o"));
    w1.target = arena.make_port(arena.make_node(arena.root(), interner.intern("d")), interner.intern("i"));
    bp = bp.with_wire(w1);

    const std::string encoded = bp2::BlueprintCodec::encode(bp, interner, arena);
    const auto j = nlohmann::json::parse(encoded);

    ASSERT_EQ(j["wires"].size(), 3u);
    EXPECT_EQ(j["wires"][0]["id"], "wire_2");
    EXPECT_EQ(j["wires"][1]["id"], "wire_10");
    EXPECT_EQ(j["wires"][2]["id"], "wire_1");
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

TEST(BlueprintCodec, DecodeAppliesTypedParamDescriptors) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;

    reg.register_component(
        interner.intern("TypedComp"),
        bp2::Interface({
            {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
            {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
        }),
        "typed",
        {
            {"gain", "1.0"},
            {"enabled", "false"},
            {"mode", "auto"},
            {"table", "0:0;1:1"},
            {"offset", "0.0,0.0"},
        },
        {
            {"gain", {bp2::TypeRegistry::ParamKind::Number, "1.0", {}}},
            {"enabled", {bp2::TypeRegistry::ParamKind::Bool, "false", {}}},
            {"mode", {bp2::TypeRegistry::ParamKind::Enum, "auto", {"auto", "manual"}}},
            {"table", {bp2::TypeRegistry::ParamKind::Table, "0:0;1:1", {}}},
            {"offset", {bp2::TypeRegistry::ParamKind::Vec2, "0.0,0.0", {}}},
        }
    );

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "t1",
                "type": "TypedComp",
                "position": {"x": 10.0, "y": 20.0},
                "params": {
                    "gain": "2.5",
                    "enabled": true,
                    "mode": "manual",
                    "table": "0:0;10:20",
                    "offset": "1.0,2.0"
                }
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->nodes().size(), 1u);
    const auto& node = result->nodes()[0];

    EXPECT_FLOAT_EQ(node.params.at(interner.intern("gain")), 2.5f);
    EXPECT_EQ(node.string_params.at("enabled"), "true");
    EXPECT_EQ(node.string_params.at("mode"), "manual");
    EXPECT_EQ(node.string_params.at("table"), "0:0;10:20");
    EXPECT_EQ(node.string_params.at("offset"), "1.0,2.0");
}

TEST(BlueprintCodec, DecodeRejectsInvalidTypedEnumParam) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;

    reg.register_component(
        interner.intern("TypedComp"),
        bp2::Interface({
            {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
            {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
        }),
        "typed",
        {{"mode", "auto"}},
        {{"mode", {bp2::TypeRegistry::ParamKind::Enum, "auto", {"auto", "manual"}}}}
    );

    bp2::DecodeError err;
    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "t1",
                "type": "TypedComp",
                "position": {"x": 10.0, "y": 20.0},
                "params": {"mode": "invalid_mode"}
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("enum param"), std::string::npos);
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

TEST(BlueprintCodec, DecodeNodeMissingPosition_DefaultsToOrigin) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    reg.register_component(interner.intern("Battery"), bp2::Interface(), "");
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {"id": "n1", "type": "Battery"}
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    ASSERT_TRUE(result.has_value()) << "Decode failed: " << err.message;
    ASSERT_EQ(result->nodes().size(), 1u);
    EXPECT_FLOAT_EQ(result->nodes()[0].x, 0.0f);
    EXPECT_FLOAT_EQ(result->nodes()[0].y, 0.0f);
}

TEST(BlueprintCodec, DecodeWireInvalidRoutingPointsFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [
            {"id": "w1", "source": "/a:p", "target": "/b:q", "routing_points": [[1.0]]}
        ],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("routing_points"), std::string::npos);
}

TEST(BlueprintCodec, DecodeNestedMissingPositionFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [
            {"id": "sub1", "blueprint": "SomeBlueprint", "embedded": false}
        ]
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("position"), std::string::npos);
}

TEST(BlueprintCodec, DecodeNodeParamInvalidTypeFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0.0, "y": 0.0},
                "params": {"v_nominal": {"bad": true}}
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("params"), std::string::npos);
}

TEST(BlueprintCodec, DecodeNodePortsInvalidDirectionFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0.0, "y": 0.0},
                "ports": {
                    "p1": {"direction": "SIDEWAYS", "type": "V"}
                }
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("direction"), std::string::npos);
}

TEST(BlueprintCodec, DecodeNodePortsInvalidTypeFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0.0, "y": 0.0},
                "ports": {
                    "p1": {"direction": "Out", "type": "NotAType"}
                }
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("type"), std::string::npos);
}

TEST(BlueprintCodec, DecodeNestedUnknownBlueprintFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [
            {
                "id": "sub1",
                "blueprint": "DefinitelyUnknownBlueprint",
                "embedded": false,
                "position": {"x": 10.0, "y": 20.0}
            }
        ]
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("unknown nested blueprint"), std::string::npos);
}

TEST(BlueprintCodec, DecodeNodeLayoutOverrideInvalidSideFails) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0.0, "y": 0.0},
                "layout_overrides": [
                    {"port_name": "v_out", "side": "diagonal"}
                ]
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("layout_overrides"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsDuplicateNodeIdsEarly) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "dup",
        "display_name": "Dup",
        "interface": [],
        "nodes": [
            {"id": "n1", "type": "Battery", "position": {"x": 0, "y": 0}},
            {"id": "n1", "type": "Resistor", "position": {"x": 10, "y": 0}}
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("duplicate node ID"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsDuplicateWireIdsEarly) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "dup_w",
        "display_name": "DupW",
        "interface": [],
        "nodes": [],
        "wires": [
            {"id": "w1", "source": "/a:p", "target": "/b:q"},
            {"id": "w1", "source": "/c:p", "target": "/d:q"}
        ],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("duplicate wire ID"), std::string::npos);
}

TEST(BlueprintCodec, EncodeDeterministicParamKeyOrderingAcrossRepeatedCalls) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("repeat_order_test"));
    bp = bp.with_display_name("Repeat Order Test");

    bp2::Blueprint::Node n;
    n.id = interner.intern("n1");
    n.type = interner.intern("Battery");
    n.params[interner.intern("z")]=1.0f;
    n.params[interner.intern("a")]=2.0f;
    n.params[interner.intern("m")]=3.0f;
    bp = bp.with_node(std::move(n));

    std::string first = bp2::BlueprintCodec::encode(bp, interner, arena);
    std::string second = bp2::BlueprintCodec::encode(bp, interner, arena);
    EXPECT_EQ(first, second);
}

TEST(BlueprintCodec, DecodeRejectsUnknownTopLevelFields) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [],
        "legacy_field": 123
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("unknown top-level"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsUnknownNodeFields) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "legacy": true
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("unknown node field"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsUnknownNodePortFields) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "ports": {
                    "p1": {"direction": "Out", "type": "V", "legacy": true}
                }
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("unknown node port field"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsUnknownLayoutOverrideFields) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "layout_overrides": [
                    {"port_name": "v_out", "side": "left", "legacy": 1}
                ]
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("unknown layout_overrides field"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsLayoutOverrideNonObjectEntry) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "layout_overrides": [42]
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("layout_overrides item"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsLayoutOverrideMissingPortName) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "layout_overrides": [
                    {"side": "left"}
                ]
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("missing string field 'port_name'"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsLayoutOverrideNonIntegerPosition) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "layout_overrides": [
                    {"port_name": "v_out", "position": 1.5}
                ]
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("layout_overrides.position"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsPortDescriptorNonObject) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "ports": {
                    "p1": 42
                }
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("port descriptor"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsUnknownWireFields) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [
            {"id": "w1", "source": "/a:p", "target": "/b:q", "extra": 1}
        ],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("unknown wire field"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsUnknownNestedFields) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [
            {
                "id": "sub1",
                "blueprint": "KnownBp",
                "embedded": true,
                "position": {"x": 0, "y": 0},
                "extra": 1,
                "definition": {
                    "version": "3.0",
                    "id": "inner",
                    "display_name": "Inner",
                    "interface": [],
                    "nodes": [],
                    "wires": [],
                    "nested": []
                }
            }
        ]
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("unknown nested field"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsUnknownInterfaceFields) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [
            {"name": "p", "domain": 1, "direction": 0, "extra": true}
        ],
        "nodes": [],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("unknown interface field"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsInterfaceUnknownDomainValue) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [
            {"name": "p", "domain": 99, "direction": 0}
        ],
        "nodes": [],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("unknown domain"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsInterfaceUnknownDirectionValue) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [
            {"name": "p", "domain": 1, "direction": 99}
        ],
        "nodes": [],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("unknown direction"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsNonFiniteNodePosition) {
#if defined(__FAST_MATH__)
    GTEST_SKIP() << "Non-finite checks are undefined under -ffast-math";
#endif
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 1e50, "y": 0}
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("non-finite"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsNonFiniteWireRoutingPoint) {
#if defined(__FAST_MATH__)
    GTEST_SKIP() << "Non-finite checks are undefined under -ffast-math";
#endif
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [
            {"id": "w1", "source": "/a:p", "target": "/b:q", "routing_points": [[1e50, 0]]}
        ],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("non-finite"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsNonFiniteNestedPosition) {
#if defined(__FAST_MATH__)
    GTEST_SKIP() << "Non-finite checks are undefined under -ffast-math";
#endif
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [
            {
                "id": "sub1",
                "blueprint": "inner",
                "embedded": true,
                "position": {"x": 0, "y": -1e50},
                "definition": {
                    "version": "3.0",
                    "id": "inner",
                    "display_name": "Inner",
                    "interface": [],
                    "nodes": [],
                    "wires": [],
                    "nested": []
                }
            }
        ]
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("non-finite"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsNonFiniteViewportValues) {
#if defined(__FAST_MATH__)
    GTEST_SKIP() << "Non-finite checks are undefined under -ffast-math";
#endif
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [],
        "zoom": 1e50
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("non-finite"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsNonNumericViewportFieldType) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [],
        "zoom": "fast"
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("viewport"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsEmbeddedNestedMissingDefinition) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [
            {
                "id": "sub1",
                "blueprint": "inner",
                "embedded": true,
                "position": {"x": 0, "y": 0}
            }
        ]
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("definition"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsNonEmbeddedNestedWithDefinition) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [
            {
                "id": "sub1",
                "blueprint": "KnownBp",
                "embedded": false,
                "position": {"x": 0, "y": 0},
                "definition": {
                    "version": "3.0",
                    "id": "inner",
                    "display_name": "Inner",
                    "interface": [],
                    "nodes": [],
                    "wires": [],
                    "nested": []
                }
            }
        ]
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("definition"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsNodePortUnknownNumericType) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "ports": {
                    "p1": {"direction": "Out", "type": 999}
                }
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("port type"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsNodePortNonStringDirection) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "ports": {
                    "p1": {"direction": true, "type": "V"}
                }
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("port direction"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsNonPositiveZoom) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [],
        "zoom": 0.0
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("zoom"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsNonPositiveGridStep) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [],
        "grid_step": -16.0
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("grid_step"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsInvalidStringParamsValueType) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "string_params": {
                    "table": 42
                }
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("string_params"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsInvalidNodeContentFieldType) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "content_value": "bad"
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("content_value"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsNodeContentMinGreaterThanMax) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "content_min": 10,
                "content_max": 5
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("content_min"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsEmbeddedDefinitionInvalidType) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [
            {
                "id": "sub1",
                "blueprint": "inner",
                "embedded": true,
                "position": {"x": 0, "y": 0},
                "definition": 42
            }
        ]
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("embedded definition"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsEmbeddedDefinitionWithContextualError) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [
            {
                "id": "sub1",
                "blueprint": "inner",
                "embedded": true,
                "position": {"x": 0, "y": 0},
                "definition": {
                    "version": "3.0",
                    "id": "inner",
                    "display_name": "Inner",
                    "interface": [],
                    "nodes": [],
                    "wires": [],
                    "nested": [],
                    "legacy": 1
                }
            }
        ]
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("embedded definition"), std::string::npos);
    EXPECT_NE(err.message.find("unknown top-level"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsExcessiveZoom) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [],
        "zoom": 1001.0
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("zoom"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsExcessiveGridStep) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": [],
        "grid_step": 10001.0
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("grid_step"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsTopLevelNameWrongType) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "name": 42,
        "interface": [],
        "nodes": [],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("name"), std::string::npos);
}

TEST(BlueprintCodec, DecodeRejectsOptionalNodeWidthWrongType) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    bp2::DecodeError err;

    std::string json = R"({
        "version": "3.0",
        "id": "test",
        "display_name": "Test",
        "interface": [],
        "nodes": [
            {
                "id": "n1",
                "type": "Battery",
                "position": {"x": 0, "y": 0},
                "width": "wide"
            }
        ],
        "wires": [],
        "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("width"), std::string::npos);
}

TEST(BlueprintCodec, AllowsLibraryMetadataFields) {
     ui::StringInterner interner;
     bp2::PathArena arena(interner);
     bp2::TypeRegistry reg;

     std::string json = R"({
         "version": "3.0",
         "id": "math_filter",
         "display_name": "Math Filter",
         "cpp_class": "FirstOrderLagComponent",
         "description": "First-order lag filter",
         "domains": ["Electrical", "Logical"],
         "scheduler_source": "SimClock",
         "param_defaults": {"tau": 0.1, "gain": 1.0},
         "param_schema": [{"name": "tau", "type": "Number"}],
         "solver_role": "passive",
         "interface": [
             {
                 "name": "input",
                 "domain": 1,
                 "direction": 0,
                 "type": "V",
                 "source_writer": true
             }
         ],
         "nodes": [],
         "wires": [],
         "nested": []
     })";

     auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg);
     ASSERT_TRUE(result.has_value());
     EXPECT_EQ(interner.resolve(result->id()), "math_filter");
     EXPECT_EQ(result->display_name(), "Math Filter");
 }

TEST(BlueprintCodec, DecodeRejectsOptionalNodeColorWrongType) {
     ui::StringInterner interner;
     bp2::PathArena arena(interner);
     bp2::TypeRegistry reg;
     bp2::DecodeError err;

     std::string json = R"({
         "version": "3.0",
         "id": "test",
         "display_name": "Test",
         "interface": [],
         "nodes": [
             {
                 "id": "n1",
                 "type": "Battery",
                 "position": {"x": 0, "y": 0},
                 "has_color": true,
                 "color_r": "red"
             }
         ],
         "wires": [],
         "nested": []
    })";

    auto result = bp2::BlueprintCodec::decode(json, interner, arena, reg, &err);
    EXPECT_FALSE(result.has_value());
    EXPECT_NE(err.message.find("color_r"), std::string::npos);
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
    bool has_v_in = false;
    bool has_v_out = false;
    bool has_gnd = false;
    for (const auto& p : decoded->iface().ports()) {
        std::string name(interner.resolve(p.name));
        if (name == "v_in" && p.domain == Domain::Electrical && p.direction == bp2::Direction::Input) {
            has_v_in = true;
        }
        if (name == "v_out" && p.domain == Domain::Electrical && p.direction == bp2::Direction::Output) {
            has_v_out = true;
        }
        if (name == "gnd" && p.domain == Domain::Electrical && p.direction == bp2::Direction::InOut) {
            has_gnd = true;
        }
    }
    EXPECT_TRUE(has_v_in);
    EXPECT_TRUE(has_v_out);
    EXPECT_TRUE(has_gnd);
}

TEST(BlueprintCodec, EncodeDeterministicNodeAndPreservedWireOrdering) {
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
    EXPECT_EQ(j["wires"][0]["id"], "wire_20");
    EXPECT_EQ(j["wires"][1]["id"], "wire_10");
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

TEST(BlueprintCodec, EncodeNormalizesTypedParamsWithRegistry) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;

    reg.register_component(
        interner.intern("TypedComp"),
        bp2::Interface({
            {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
            {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
        }),
        "typed",
        {},
        {
            {"gain", {bp2::TypeRegistry::ParamKind::Number, "1.0", {}}},
            {"enabled", {bp2::TypeRegistry::ParamKind::Bool, "false", {}}},
            {"mode", {bp2::TypeRegistry::ParamKind::Enum, "auto", {"auto", "manual"}}},
            {"offset", {bp2::TypeRegistry::ParamKind::Vec2, "0.0,0.0", {}}},
        }
    );

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("typed_encode"));
    bp = bp.with_display_name("Typed Encode");

    bp2::Blueprint::Node n;
    n.id = interner.intern("n1");
    n.type = interner.intern("TypedComp");
    n.params[interner.intern("gain")] = 2.5f;
    n.string_params["enabled"] = "1";
    n.string_params["mode"] = "manual";
    n.string_params["offset"] = "1.0,2.0";
    n.string_params["other"] = "kept_in_string_params";
    bp = bp.with_node(std::move(n));

    std::string encoded = bp2::BlueprintCodec::encode(bp, interner, arena, &reg);
    auto j = nlohmann::json::parse(encoded);

    ASSERT_EQ(j["nodes"].size(), 1u);
    auto& node = j["nodes"][0];
    ASSERT_TRUE(node.contains("params"));
    EXPECT_FLOAT_EQ(node["params"]["gain"].get<float>(), 2.5f);
    EXPECT_TRUE(node["params"]["enabled"].get<bool>());
    EXPECT_EQ(node["params"]["mode"].get<std::string>(), "manual");
    EXPECT_EQ(node["params"]["offset"].get<std::string>(), "1.0,2.0");

    ASSERT_TRUE(node.contains("string_params"));
    EXPECT_TRUE(node["string_params"].contains("other"));
    EXPECT_FALSE(node["string_params"].contains("enabled"));
    EXPECT_FALSE(node["string_params"].contains("mode"));
    EXPECT_FALSE(node["string_params"].contains("offset"));
}

TEST(BlueprintCodec, EncodeDeterministicInterfaceAndNodePortOrdering) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("port_order_test"));
    bp = bp.with_display_name("Port Order Test");
    bp = bp.with_interface(bp2::Interface({
        {interner.intern("z_if"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("a_if"), Domain::Electrical, bp2::Direction::Output},
    }));

    bp2::Blueprint::Node n;
    n.id = interner.intern("n1");
    n.type = interner.intern("Battery");
    n.inputs.emplace_back(interner.intern("z_in"), PortSide::Input, PortType::V);
    n.outputs.emplace_back(interner.intern("a_out"), PortSide::Output, PortType::V);
    bp = bp.with_node(std::move(n));

    std::string json_str = bp2::BlueprintCodec::encode(bp, interner, arena);
    auto j = nlohmann::json::parse(json_str);

    ASSERT_EQ(j["interface"].size(), 2u);
    EXPECT_EQ(j["interface"][0]["name"], "a_if");
    EXPECT_EQ(j["interface"][1]["name"], "z_if");

    std::string ports_dump = j["nodes"][0]["ports"].dump();
    EXPECT_LT(ports_dump.find("\"a_out\""), ports_dump.find("\"z_in\""));
}

TEST(BlueprintCodec, EncodeGoldenSnapshotStable) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("golden_bp"));
    bp = bp.with_display_name("Golden BP");
    bp = bp.with_name("GoldenName");

    bp2::Blueprint::Node n;
    n.id = interner.intern("n1");
    n.type = interner.intern("Battery");
    n.x = 12.5f;
    n.y = -3.25f;
    n.params[interner.intern("zeta")] = 1.0f;
    n.params[interner.intern("alpha")] = 2.0f;
    n.string_params["beta"] = "x";
    bp = bp.with_node(std::move(n));

    std::string actual = bp2::BlueprintCodec::encode(bp, interner, arena);
    const std::string expected = R"({
  "display_name": "Golden BP",
  "grid_step": 16.0,
  "id": "golden_bp",
  "interface": [],
  "name": "GoldenName",
  "nested": [],
  "nodes": [
    {
      "id": "n1",
      "params": {
        "alpha": 2.0,
        "zeta": 1.0
      },
      "position": {
        "x": 12.5,
        "y": -3.25
      },
      "string_params": {
        "beta": "x"
      },
      "type": "Battery"
    }
  ],
  "pan_x": 0.0,
  "pan_y": 0.0,
  "version": "3.0",
  "wires": [],
  "zoom": 1.0
})";

    EXPECT_EQ(actual, expected);
}

// =============================================================================
// Regression: Library blueprints with missing position fields
// =============================================================================

TEST(BlueprintCodec, DecodeNodeWithoutPosition_DefaultsToZero) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    reg.register_component(interner.intern("Subtract"), bp2::Interface(), "");

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
    bp2::TypeRegistry reg;
    reg.register_component(interner.intern("Subtract"), bp2::Interface(), "");

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
    bp2::TypeRegistry reg;
    reg.register_component(interner.intern("Battery"), bp2::Interface(), "");

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
    bp2::TypeRegistry reg;
    reg.register_component(interner.intern("SomeType"), bp2::Interface(), "");

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
    bp2::TypeRegistry reg;
    // Register Battery (standard library type)
    reg.register_component(
        interner.intern("Battery"),
        bp2::Interface({
            {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output},
        }),
        "Battery"
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
    bp2::PathResolver resolver;
    const auto& wire = loaded->wires()[0];
    auto src = resolver.resolve(wire.source, *loaded, arena, reg);
    auto tgt = resolver.resolve(wire.target, *loaded, arena, reg);
    EXPECT_TRUE(src.has_value()) << "Source endpoint unresolved after roundtrip";
    EXPECT_TRUE(tgt.has_value()) << "Target endpoint unresolved after roundtrip (THE BUG)";

    // Also verify via WireValidator (the actual code path that produces the error)
    auto vr = bp2::WireValidator::validate(wire, *loaded, arena, reg);
    EXPECT_TRUE(vr.valid) << "Wire validation failed: " << vr.error;
}

TEST(BlueprintCodec, RoundTripProxyNodeWithWires_DoubleRoundTrip) {
    // Encode → Decode → Encode → Decode: verify stability across two roundtrips.
    // This catches any loss of iface data that could accumulate.
    // Uses expandable proxy nodes with embedded nested definitions.
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    reg.register_component(
        interner.intern("Generator"),
        bp2::Interface({
            {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output},
        }),
        "Generator"
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
    bp2::PathResolver resolver;
    const auto& wire = bp2_result->wires()[0];
    auto src = resolver.resolve(wire.source, *bp2_result, arena, reg);
    auto tgt = resolver.resolve(wire.target, *bp2_result, arena, reg);
    EXPECT_TRUE(src.has_value()) << "Source unresolved after double roundtrip";
    EXPECT_TRUE(tgt.has_value()) << "Target unresolved after double roundtrip";

    // Validate wire
    auto vr = bp2::WireValidator::validate(wire, *bp2_result, arena, reg);
    EXPECT_TRUE(vr.valid) << "Wire validation failed after double roundtrip: " << vr.error;
}

TEST(BlueprintCodec, DecodeLibraryBlueprintFormat_FullExample) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry reg;
    reg.register_component(interner.intern("BlueprintInput"), bp2::Interface(), "");
    reg.register_component(interner.intern("BlueprintOutput"), bp2::Interface(), "");

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
    bp2::TypeRegistry reg;

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
    bp2::TypeRegistry reg;

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
