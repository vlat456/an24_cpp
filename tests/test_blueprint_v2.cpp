#include <gtest/gtest.h>
#include "blueprint_v2.h"
#include <nlohmann/json.hpp>

using namespace an24::v2;

// ==================================================================
// Helper: raw JSON strings for each test scenario
// ==================================================================

static const char* kMinimalCppComponent = R"({
    "version": 2,
    "meta": {
        "name": "Battery",
        "description": "28V aircraft battery",
        "domains": ["Electrical"],
        "cpp_class": true
    },
    "exposes": {
        "v_out": {"direction": "Out", "type": "V"},
        "v_in":  {"direction": "In",  "type": "V"}
    },
    "params": {
        "v_nominal": {"type": "float", "default": "28.0"},
        "capacity":  {"type": "float", "default": "1000.0"}
    }
})";

static const char* kCompositeBlueprint = R"({
    "version": 2,
    "meta": {
        "name": "lamp_pass_through",
        "description": "Voltage pass-through with indicator lamp",
        "domains": ["Electrical"],
        "cpp_class": false
    },
    "exposes": {
        "vin":  {"direction": "In",  "type": "V"},
        "vout": {"direction": "Out", "type": "V"}
    },
    "nodes": {
        "vin":  {"type": "BlueprintInput",  "params": {"exposed_type": "V"}},
        "lamp": {"type": "IndicatorLight",  "params": {"color": "red"}},
        "vout": {"type": "BlueprintOutput", "params": {"exposed_type": "V"}}
    },
    "wires": [
        {"id": "w_in",  "from": ["vin",  "port"],  "to": ["lamp", "v_in"]},
        {"id": "w_out", "from": ["lamp", "v_out"], "to": ["vout", "port"]}
    ]
})";

static const char* kRootDocument = R"({
    "version": 2,
    "meta": {
        "name": "an24_main",
        "description": "AN-24 main electrical system",
        "domains": ["Electrical", "Mechanical"]
    },
    "viewport": {
        "pan": [70.7, -185.6],
        "zoom": 0.85,
        "grid": 16
    },
    "nodes": {
        "bat1": {
            "type": "Battery",
            "pos": [96, 112],
            "size": [128, 80],
            "params": {"v_nominal": "28.0"}
        },
        "bus1": {
            "type": "Bus",
            "pos": [320, -80],
            "size": [112, 32]
        }
    },
    "wires": [
        {
            "id": "w1",
            "from": ["bat1", "v_out"],
            "to":   ["bus1", "v"],
            "routing": [[416, 144]]
        }
    ],
    "sub_blueprints": {
        "lamp_1": {
            "template": "library/systems/lamp_pass_through",
            "pos": [400, 300],
            "size": [200, 150],
            "collapsed": true,
            "overrides": {
                "params": {"lamp.color": "green"},
                "layout": {"lamp": [250, 100]},
                "routing": {"w_in": [[100, 110]]}
            }
        }
    }
})";

static const char* kSubBlueprintEmbedded = R"({
    "version": 2,
    "meta": {"name": "test_embedded", "domains": ["Electrical"]},
    "sub_blueprints": {
        "lamp_1": {
            "template": "library/systems/lamp_pass_through",
            "pos": [400, 300],
            "size": [200, 150],
            "collapsed": false,
            "nodes": {
                "vin":  {"type": "BlueprintInput",  "pos": [50, 100],  "params": {"exposed_type": "V"}},
                "lamp": {"type": "IndicatorLight",  "pos": [250, 100], "params": {"color": "green"}},
                "vout": {"type": "BlueprintOutput", "pos": [450, 100], "params": {"exposed_type": "V"}}
            },
            "wires": [
                {"id": "w_in",  "from": ["vin",  "port"],  "to": ["lamp", "v_in"]},
                {"id": "w_out", "from": ["lamp", "v_out"], "to": ["vout", "port"]}
            ]
        }
    }
})";

static const char* kNodeWithContent = R"({
    "version": 2,
    "meta": {"name": "test_content", "domains": ["Electrical"]},
    "nodes": {
        "voltmeter1": {
            "type": "Voltmeter",
            "pos": [100, 200],
            "content": {
                "kind": "gauge",
                "label": "V",
                "unit": "V",
                "min": 0,
                "max": 30,
                "value": 12.5
            }
        }
    }
})";

static const char* kNodeWithColor = R"({
    "version": 2,
    "meta": {"name": "test_color", "domains": ["Electrical"]},
    "nodes": {
        "bat1": {
            "type": "Battery",
            "pos": [10, 20],
            "color": {"r": 0.2, "g": 0.4, "b": 0.6, "a": 0.8}
        }
    }
})";

static const char* kPortWithAlias = R"({
    "version": 2,
    "meta": {
        "name": "Splitter",
        "domains": ["Electrical"],
        "cpp_class": true
    },
    "exposes": {
        "v_in":  {"direction": "In",  "type": "V"},
        "v_out1": {"direction": "Out", "type": "V", "alias": "v_in"},
        "v_out2": {"direction": "Out", "type": "V", "alias": "v_in"}
    }
})";

// ==================================================================
// Parse tests
// ==================================================================

TEST(BlueprintV2Parse, MinimalCppComponent) {
    auto bp = parse_blueprint_v2(kMinimalCppComponent);
    ASSERT_TRUE(bp.has_value()) << "Failed to parse minimal C++ component";

    EXPECT_EQ(bp->version, 2);
    EXPECT_EQ(bp->meta.name, "Battery");
    EXPECT_EQ(bp->meta.description, "28V aircraft battery");
    EXPECT_TRUE(bp->meta.cpp_class);
    ASSERT_EQ(bp->meta.domains.size(), 1u);
    EXPECT_EQ(bp->meta.domains[0], "Electrical");

    // Exposes
    ASSERT_EQ(bp->exposes.size(), 2u);
    EXPECT_EQ(bp->exposes.at("v_out").direction, "Out");
    EXPECT_EQ(bp->exposes.at("v_out").type, "V");
    EXPECT_EQ(bp->exposes.at("v_in").direction, "In");
    EXPECT_EQ(bp->exposes.at("v_in").type, "V");

    // Params
    ASSERT_EQ(bp->params.size(), 2u);
    EXPECT_EQ(bp->params.at("v_nominal").type, "float");
    EXPECT_EQ(bp->params.at("v_nominal").default_val, "28.0");
    EXPECT_EQ(bp->params.at("capacity").default_val, "1000.0");

    // No nodes, wires, sub_blueprints, viewport
    EXPECT_TRUE(bp->nodes.empty());
    EXPECT_TRUE(bp->wires.empty());
    EXPECT_TRUE(bp->sub_blueprints.empty());
    EXPECT_FALSE(bp->viewport.has_value());
}

TEST(BlueprintV2Parse, CompositeBlueprint) {
    auto bp = parse_blueprint_v2(kCompositeBlueprint);
    ASSERT_TRUE(bp.has_value()) << "Failed to parse composite blueprint";

    EXPECT_EQ(bp->meta.name, "lamp_pass_through");
    EXPECT_FALSE(bp->meta.cpp_class);

    // Exposes
    ASSERT_EQ(bp->exposes.size(), 2u);
    EXPECT_EQ(bp->exposes.at("vin").direction, "In");
    EXPECT_EQ(bp->exposes.at("vout").direction, "Out");

    // Nodes (object keyed by id)
    ASSERT_EQ(bp->nodes.size(), 3u);
    EXPECT_EQ(bp->nodes.at("vin").type, "BlueprintInput");
    EXPECT_EQ(bp->nodes.at("lamp").type, "IndicatorLight");
    EXPECT_EQ(bp->nodes.at("lamp").params.at("color"), "red");
    EXPECT_EQ(bp->nodes.at("vout").type, "BlueprintOutput");

    // Wires
    ASSERT_EQ(bp->wires.size(), 2u);
    EXPECT_EQ(bp->wires[0].id, "w_in");
    EXPECT_EQ(bp->wires[0].from.node, "vin");
    EXPECT_EQ(bp->wires[0].from.port, "port");
    EXPECT_EQ(bp->wires[0].to.node, "lamp");
    EXPECT_EQ(bp->wires[0].to.port, "v_in");

    EXPECT_EQ(bp->wires[1].id, "w_out");
    EXPECT_EQ(bp->wires[1].from.node, "lamp");
    EXPECT_EQ(bp->wires[1].from.port, "v_out");
}

TEST(BlueprintV2Parse, RootDocument) {
    auto bp = parse_blueprint_v2(kRootDocument);
    ASSERT_TRUE(bp.has_value()) << "Failed to parse root document";

    // Meta
    EXPECT_EQ(bp->meta.name, "an24_main");
    ASSERT_EQ(bp->meta.domains.size(), 2u);
    EXPECT_EQ(bp->meta.domains[0], "Electrical");
    EXPECT_EQ(bp->meta.domains[1], "Mechanical");

    // Viewport
    ASSERT_TRUE(bp->viewport.has_value());
    EXPECT_FLOAT_EQ(bp->viewport->pan[0], 70.7f);
    EXPECT_FLOAT_EQ(bp->viewport->pan[1], -185.6f);
    EXPECT_FLOAT_EQ(bp->viewport->zoom, 0.85f);
    EXPECT_FLOAT_EQ(bp->viewport->grid, 16.0f);

    // Nodes
    ASSERT_EQ(bp->nodes.size(), 2u);
    EXPECT_EQ(bp->nodes.at("bat1").type, "Battery");
    EXPECT_FLOAT_EQ(bp->nodes.at("bat1").pos[0], 96.0f);
    EXPECT_FLOAT_EQ(bp->nodes.at("bat1").pos[1], 112.0f);
    EXPECT_FLOAT_EQ(bp->nodes.at("bat1").size[0], 128.0f);
    EXPECT_FLOAT_EQ(bp->nodes.at("bat1").size[1], 80.0f);
    EXPECT_EQ(bp->nodes.at("bat1").params.at("v_nominal"), "28.0");

    EXPECT_EQ(bp->nodes.at("bus1").type, "Bus");

    // Wires with routing points
    ASSERT_EQ(bp->wires.size(), 1u);
    EXPECT_EQ(bp->wires[0].id, "w1");
    EXPECT_EQ(bp->wires[0].from.node, "bat1");
    EXPECT_EQ(bp->wires[0].from.port, "v_out");
    EXPECT_EQ(bp->wires[0].to.node, "bus1");
    EXPECT_EQ(bp->wires[0].to.port, "v");
    ASSERT_EQ(bp->wires[0].routing.size(), 1u);
    EXPECT_FLOAT_EQ(bp->wires[0].routing[0][0], 416.0f);
    EXPECT_FLOAT_EQ(bp->wires[0].routing[0][1], 144.0f);

    // Sub-blueprints (reference mode)
    ASSERT_EQ(bp->sub_blueprints.size(), 1u);
    const auto& sb = bp->sub_blueprints.at("lamp_1");
    ASSERT_TRUE(sb.template_path.has_value());
    EXPECT_EQ(*sb.template_path, "library/systems/lamp_pass_through");
    EXPECT_FLOAT_EQ(sb.pos[0], 400.0f);
    EXPECT_FLOAT_EQ(sb.pos[1], 300.0f);
    EXPECT_FLOAT_EQ(sb.size[0], 200.0f);
    EXPECT_FLOAT_EQ(sb.size[1], 150.0f);
    EXPECT_TRUE(sb.collapsed);
    EXPECT_TRUE(sb.nodes.empty());  // reference mode: no inline nodes
    EXPECT_FALSE(sb.is_embedded());
}

TEST(BlueprintV2Parse, SubBlueprintReference) {
    auto bp = parse_blueprint_v2(kRootDocument);
    ASSERT_TRUE(bp.has_value());

    const auto& sb = bp->sub_blueprints.at("lamp_1");
    ASSERT_TRUE(sb.overrides.has_value());

    // params override
    ASSERT_EQ(sb.overrides->params.size(), 1u);
    EXPECT_EQ(sb.overrides->params.at("lamp.color"), "green");

    // layout override
    ASSERT_EQ(sb.overrides->layout.size(), 1u);
    EXPECT_FLOAT_EQ(sb.overrides->layout.at("lamp")[0], 250.0f);
    EXPECT_FLOAT_EQ(sb.overrides->layout.at("lamp")[1], 100.0f);

    // routing override
    ASSERT_EQ(sb.overrides->routing.size(), 1u);
    ASSERT_EQ(sb.overrides->routing.at("w_in").size(), 1u);
    EXPECT_FLOAT_EQ(sb.overrides->routing.at("w_in")[0][0], 100.0f);
    EXPECT_FLOAT_EQ(sb.overrides->routing.at("w_in")[0][1], 110.0f);
}

TEST(BlueprintV2Parse, SubBlueprintEmbedded) {
    auto bp = parse_blueprint_v2(kSubBlueprintEmbedded);
    ASSERT_TRUE(bp.has_value());

    ASSERT_EQ(bp->sub_blueprints.size(), 1u);
    const auto& sb = bp->sub_blueprints.at("lamp_1");

    // Embedded mode: has nodes
    EXPECT_TRUE(sb.is_embedded());
    ASSERT_EQ(sb.nodes.size(), 3u);
    EXPECT_EQ(sb.nodes.at("vin").type, "BlueprintInput");
    EXPECT_FLOAT_EQ(sb.nodes.at("vin").pos[0], 50.0f);
    EXPECT_EQ(sb.nodes.at("lamp").params.at("color"), "green");
    EXPECT_EQ(sb.nodes.at("vout").type, "BlueprintOutput");

    // Wires inside embedded sub-blueprint
    ASSERT_EQ(sb.wires.size(), 2u);
    EXPECT_EQ(sb.wires[0].from.node, "vin");
    EXPECT_EQ(sb.wires[1].to.node, "vout");

    // template kept for provenance
    ASSERT_TRUE(sb.template_path.has_value());
    EXPECT_EQ(*sb.template_path, "library/systems/lamp_pass_through");

    // collapsed=false for embedded
    EXPECT_FALSE(sb.collapsed);
}

TEST(BlueprintV2Parse, WireEndpointsAsArrays) {
    auto bp = parse_blueprint_v2(kCompositeBlueprint);
    ASSERT_TRUE(bp.has_value());

    // Wire endpoints are parsed from ["node", "port"] arrays
    const auto& w = bp->wires[0];
    EXPECT_EQ(w.from.node, "vin");
    EXPECT_EQ(w.from.port, "port");
    EXPECT_EQ(w.to.node, "lamp");
    EXPECT_EQ(w.to.port, "v_in");
}

TEST(BlueprintV2Parse, PositionsAsArrays) {
    auto bp = parse_blueprint_v2(kRootDocument);
    ASSERT_TRUE(bp.has_value());

    // Positions parsed from [x, y] arrays
    EXPECT_FLOAT_EQ(bp->nodes.at("bat1").pos[0], 96.0f);
    EXPECT_FLOAT_EQ(bp->nodes.at("bat1").pos[1], 112.0f);
    EXPECT_FLOAT_EQ(bp->nodes.at("bat1").size[0], 128.0f);
    EXPECT_FLOAT_EQ(bp->nodes.at("bat1").size[1], 80.0f);
}

TEST(BlueprintV2Parse, NodeContent) {
    auto bp = parse_blueprint_v2(kNodeWithContent);
    ASSERT_TRUE(bp.has_value());

    const auto& node = bp->nodes.at("voltmeter1");
    ASSERT_TRUE(node.content.has_value());
    EXPECT_EQ(node.content->kind, "gauge");
    EXPECT_EQ(node.content->label, "V");
    EXPECT_EQ(node.content->unit, "V");
    EXPECT_FLOAT_EQ(node.content->min, 0.0f);
    EXPECT_FLOAT_EQ(node.content->max, 30.0f);
    EXPECT_FLOAT_EQ(node.content->value, 12.5f);
}

TEST(BlueprintV2Parse, NodeColor) {
    auto bp = parse_blueprint_v2(kNodeWithColor);
    ASSERT_TRUE(bp.has_value());

    const auto& node = bp->nodes.at("bat1");
    ASSERT_TRUE(node.color.has_value());
    EXPECT_FLOAT_EQ(node.color->r, 0.2f);
    EXPECT_FLOAT_EQ(node.color->g, 0.4f);
    EXPECT_FLOAT_EQ(node.color->b, 0.6f);
    EXPECT_FLOAT_EQ(node.color->a, 0.8f);
}

TEST(BlueprintV2Parse, PortAlias) {
    auto bp = parse_blueprint_v2(kPortWithAlias);
    ASSERT_TRUE(bp.has_value());

    // v_in has no alias
    EXPECT_FALSE(bp->exposes.at("v_in").alias.has_value());

    // v_out1 and v_out2 alias to v_in
    ASSERT_TRUE(bp->exposes.at("v_out1").alias.has_value());
    EXPECT_EQ(*bp->exposes.at("v_out1").alias, "v_in");
    ASSERT_TRUE(bp->exposes.at("v_out2").alias.has_value());
    EXPECT_EQ(*bp->exposes.at("v_out2").alias, "v_in");
}

TEST(BlueprintV2Parse, InvalidVersionReturnsNullopt) {
    const char* v1_json = R"({"version": 1, "meta": {"name": "old"}})";
    auto bp = parse_blueprint_v2(v1_json);
    EXPECT_FALSE(bp.has_value());
}

TEST(BlueprintV2Parse, MalformedJsonReturnsNullopt) {
    auto bp = parse_blueprint_v2("not json at all {{{");
    EXPECT_FALSE(bp.has_value());
}

TEST(BlueprintV2Parse, MissingVersionReturnsNullopt) {
    const char* no_version = R"({"meta": {"name": "test"}})";
    auto bp = parse_blueprint_v2(no_version);
    EXPECT_FALSE(bp.has_value());
}

// ==================================================================
// Serialize tests
// ==================================================================

TEST(BlueprintV2Serialize, Roundtrip) {
    // Build a BlueprintV2 programmatically
    BlueprintV2 bp;
    bp.version = 2;
    bp.meta.name = "roundtrip_test";
    bp.meta.description = "Tests roundtrip fidelity";
    bp.meta.domains = {"Electrical", "Thermal"};
    bp.meta.cpp_class = false;

    bp.exposes["vin"] = ExposedPort{"In", "V", std::nullopt};
    bp.exposes["vout"] = ExposedPort{"Out", "V", std::nullopt};

    NodeV2 bat;
    bat.type = "Battery";
    bat.pos = {100.0f, 200.0f};
    bat.size = {128.0f, 80.0f};
    bat.params["v_nominal"] = "28.0";
    bp.nodes["bat1"] = bat;

    NodeV2 bus;
    bus.type = "Bus";
    bus.pos = {300.0f, 50.0f};
    bp.nodes["bus1"] = bus;

    WireV2 w;
    w.id = "w1";
    w.from = {"bat1", "v_out"};
    w.to = {"bus1", "v"};
    w.routing = {{200.0f, 150.0f}, {250.0f, 100.0f}};
    bp.wires.push_back(w);

    bp.viewport = ViewportV2{{70.5f, -100.0f}, 0.85f, 16.0f};

    // Serialize → parse → compare
    std::string json = serialize_blueprint_v2(bp);
    ASSERT_FALSE(json.empty()) << "Serialization produced empty string";

    auto parsed = parse_blueprint_v2(json);
    ASSERT_TRUE(parsed.has_value()) << "Failed to re-parse serialized JSON:\n" << json;

    EXPECT_EQ(parsed->version, 2);
    EXPECT_EQ(parsed->meta.name, "roundtrip_test");
    EXPECT_EQ(parsed->meta.description, "Tests roundtrip fidelity");
    EXPECT_EQ(parsed->meta.domains, bp.meta.domains);
    EXPECT_FALSE(parsed->meta.cpp_class);

    // Exposes
    ASSERT_EQ(parsed->exposes.size(), 2u);
    EXPECT_EQ(parsed->exposes.at("vin").direction, "In");
    EXPECT_EQ(parsed->exposes.at("vout").direction, "Out");

    // Nodes
    ASSERT_EQ(parsed->nodes.size(), 2u);
    EXPECT_EQ(parsed->nodes.at("bat1").type, "Battery");
    EXPECT_FLOAT_EQ(parsed->nodes.at("bat1").pos[0], 100.0f);
    EXPECT_FLOAT_EQ(parsed->nodes.at("bat1").pos[1], 200.0f);
    EXPECT_FLOAT_EQ(parsed->nodes.at("bat1").size[0], 128.0f);
    EXPECT_FLOAT_EQ(parsed->nodes.at("bat1").size[1], 80.0f);
    EXPECT_EQ(parsed->nodes.at("bat1").params.at("v_nominal"), "28.0");
    EXPECT_EQ(parsed->nodes.at("bus1").type, "Bus");

    // Wires
    ASSERT_EQ(parsed->wires.size(), 1u);
    EXPECT_EQ(parsed->wires[0].id, "w1");
    EXPECT_EQ(parsed->wires[0].from.node, "bat1");
    EXPECT_EQ(parsed->wires[0].from.port, "v_out");
    EXPECT_EQ(parsed->wires[0].to.node, "bus1");
    EXPECT_EQ(parsed->wires[0].to.port, "v");
    ASSERT_EQ(parsed->wires[0].routing.size(), 2u);
    EXPECT_FLOAT_EQ(parsed->wires[0].routing[0][0], 200.0f);
    EXPECT_FLOAT_EQ(parsed->wires[0].routing[1][1], 100.0f);

    // Viewport
    ASSERT_TRUE(parsed->viewport.has_value());
    EXPECT_FLOAT_EQ(parsed->viewport->pan[0], 70.5f);
    EXPECT_FLOAT_EQ(parsed->viewport->pan[1], -100.0f);
    EXPECT_FLOAT_EQ(parsed->viewport->zoom, 0.85f);
    EXPECT_FLOAT_EQ(parsed->viewport->grid, 16.0f);
}

TEST(BlueprintV2Serialize, RoundtripWithSubBlueprints) {
    BlueprintV2 bp;
    bp.version = 2;
    bp.meta.name = "sub_bp_roundtrip";
    bp.meta.domains = {"Electrical"};

    // Reference sub-blueprint
    SubBlueprintV2 ref;
    ref.template_path = "library/systems/lamp_pass_through";
    ref.pos = {400.0f, 300.0f};
    ref.size = {200.0f, 150.0f};
    ref.collapsed = true;
    ref.overrides = OverridesV2{};
    ref.overrides->params["lamp.color"] = "green";
    ref.overrides->layout["lamp"] = {250.0f, 100.0f};
    bp.sub_blueprints["lamp_ref"] = ref;

    // Embedded sub-blueprint
    SubBlueprintV2 emb;
    emb.template_path = "library/systems/lamp_pass_through";
    emb.pos = {600.0f, 300.0f};
    emb.size = {200.0f, 150.0f};
    emb.collapsed = false;
    NodeV2 n1;
    n1.type = "BlueprintInput";
    n1.pos = {50.0f, 100.0f};
    emb.nodes["vin"] = n1;
    NodeV2 n2;
    n2.type = "IndicatorLight";
    n2.pos = {250.0f, 100.0f};
    n2.params["color"] = "red";
    emb.nodes["lamp"] = n2;
    WireV2 ew;
    ew.id = "ew1";
    ew.from = {"vin", "port"};
    ew.to = {"lamp", "v_in"};
    emb.wires.push_back(ew);
    bp.sub_blueprints["lamp_emb"] = emb;

    // Serialize → parse → compare
    std::string json = serialize_blueprint_v2(bp);
    auto parsed = parse_blueprint_v2(json);
    ASSERT_TRUE(parsed.has_value()) << "Failed roundtrip:\n" << json;

    // Reference sub-blueprint
    ASSERT_EQ(parsed->sub_blueprints.size(), 2u);
    const auto& pref = parsed->sub_blueprints.at("lamp_ref");
    ASSERT_TRUE(pref.template_path.has_value());
    EXPECT_EQ(*pref.template_path, "library/systems/lamp_pass_through");
    EXPECT_TRUE(pref.collapsed);
    EXPECT_FALSE(pref.is_embedded());
    ASSERT_TRUE(pref.overrides.has_value());
    EXPECT_EQ(pref.overrides->params.at("lamp.color"), "green");
    EXPECT_FLOAT_EQ(pref.overrides->layout.at("lamp")[0], 250.0f);

    // Embedded sub-blueprint
    const auto& pemb = parsed->sub_blueprints.at("lamp_emb");
    EXPECT_TRUE(pemb.is_embedded());
    EXPECT_FALSE(pemb.collapsed);
    ASSERT_EQ(pemb.nodes.size(), 2u);
    EXPECT_EQ(pemb.nodes.at("vin").type, "BlueprintInput");
    EXPECT_EQ(pemb.nodes.at("lamp").params.at("color"), "red");
    ASSERT_EQ(pemb.wires.size(), 1u);
    EXPECT_EQ(pemb.wires[0].from.node, "vin");
    // Provenance preserved
    ASSERT_TRUE(pemb.template_path.has_value());
    EXPECT_EQ(*pemb.template_path, "library/systems/lamp_pass_through");
}

TEST(BlueprintV2Serialize, OmitsEmptySections) {
    // A cpp_class component should not have nodes, wires, sub_blueprints, viewport in output
    BlueprintV2 bp;
    bp.version = 2;
    bp.meta.name = "Battery";
    bp.meta.cpp_class = true;
    bp.meta.domains = {"Electrical"};
    bp.exposes["v_out"] = ExposedPort{"Out", "V", std::nullopt};

    std::string json = serialize_blueprint_v2(bp);

    // The serialized JSON should not contain empty optional sections
    // Parse as raw JSON to check structure
    EXPECT_TRUE(json.find("\"nodes\"") == std::string::npos)
        << "Empty nodes should be omitted from serialized JSON";
    EXPECT_TRUE(json.find("\"wires\"") == std::string::npos)
        << "Empty wires should be omitted from serialized JSON";
    EXPECT_TRUE(json.find("\"sub_blueprints\"") == std::string::npos)
        << "Empty sub_blueprints should be omitted from serialized JSON";
    EXPECT_TRUE(json.find("\"viewport\"") == std::string::npos)
        << "Absent viewport should be omitted from serialized JSON";

    // But version and meta must be present
    EXPECT_TRUE(json.find("\"version\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"meta\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"exposes\"") != std::string::npos);
}

TEST(BlueprintV2Serialize, RoundtripNodeContentAndColor) {
    BlueprintV2 bp;
    bp.version = 2;
    bp.meta.name = "content_color_test";
    bp.meta.domains = {"Electrical"};

    NodeV2 node;
    node.type = "Voltmeter";
    node.pos = {100.0f, 200.0f};
    node.content = ContentV2{"gauge", "V", 12.5f, 0.0f, 30.0f, "V", false};
    node.color = NodeColorV2{0.1f, 0.2f, 0.3f, 0.9f};
    bp.nodes["vm1"] = node;

    std::string json = serialize_blueprint_v2(bp);
    auto parsed = parse_blueprint_v2(json);
    ASSERT_TRUE(parsed.has_value());

    const auto& pn = parsed->nodes.at("vm1");
    ASSERT_TRUE(pn.content.has_value());
    EXPECT_EQ(pn.content->kind, "gauge");
    EXPECT_EQ(pn.content->label, "V");
    EXPECT_FLOAT_EQ(pn.content->value, 12.5f);
    EXPECT_FLOAT_EQ(pn.content->min, 0.0f);
    EXPECT_FLOAT_EQ(pn.content->max, 30.0f);
    EXPECT_EQ(pn.content->unit, "V");

    ASSERT_TRUE(pn.color.has_value());
    EXPECT_FLOAT_EQ(pn.color->r, 0.1f);
    EXPECT_FLOAT_EQ(pn.color->g, 0.2f);
    EXPECT_FLOAT_EQ(pn.color->b, 0.3f);
    EXPECT_FLOAT_EQ(pn.color->a, 0.9f);
}

TEST(BlueprintV2Serialize, RoundtripPortAlias) {
    BlueprintV2 bp;
    bp.version = 2;
    bp.meta.name = "alias_test";
    bp.meta.cpp_class = true;
    bp.meta.domains = {"Electrical"};
    bp.exposes["v_in"] = ExposedPort{"In", "V", std::nullopt};
    bp.exposes["v_out"] = ExposedPort{"Out", "V", "v_in"};

    std::string json = serialize_blueprint_v2(bp);
    auto parsed = parse_blueprint_v2(json);
    ASSERT_TRUE(parsed.has_value());

    EXPECT_FALSE(parsed->exposes.at("v_in").alias.has_value());
    ASSERT_TRUE(parsed->exposes.at("v_out").alias.has_value());
    EXPECT_EQ(*parsed->exposes.at("v_out").alias, "v_in");
}

// ==================================================================
// Edge Case Tests
// ==================================================================

TEST(BlueprintV2EdgeCase, EmptyBlueprint) {
    auto bp = parse_blueprint_v2(R"({"version": 2, "meta": {"name": ""}})");
    ASSERT_TRUE(bp.has_value());
    EXPECT_TRUE(bp->nodes.empty());
    EXPECT_TRUE(bp->wires.empty());
    EXPECT_TRUE(bp->sub_blueprints.empty());
    EXPECT_TRUE(bp->exposes.empty());
    EXPECT_FALSE(bp->viewport.has_value());
}

TEST(BlueprintV2EdgeCase, MissingMeta) {
    auto bp = parse_blueprint_v2(R"({"version": 2})");
    ASSERT_TRUE(bp.has_value());
    EXPECT_TRUE(bp->meta.name.empty());
}

TEST(BlueprintV2EdgeCase, WrongVersion) {
    auto bp = parse_blueprint_v2(R"({"version": 1, "meta": {"name": "old"}})");
    EXPECT_FALSE(bp.has_value());
}

TEST(BlueprintV2EdgeCase, MissingVersion) {
    auto bp = parse_blueprint_v2(R"({"meta": {"name": "nover"}})");
    EXPECT_FALSE(bp.has_value());
}

TEST(BlueprintV2EdgeCase, InvalidJson) {
    auto bp = parse_blueprint_v2("{not valid json");
    EXPECT_FALSE(bp.has_value());
}

TEST(BlueprintV2EdgeCase, EmptyString) {
    auto bp = parse_blueprint_v2("");
    EXPECT_FALSE(bp.has_value());
}

TEST(BlueprintV2EdgeCase, NodeWithNoParams) {
    auto bp = parse_blueprint_v2(R"({
        "version": 2, "meta": {"name": ""},
        "nodes": { "n1": {"type": "Bus", "pos": [0, 0]} }
    })");
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1);
    EXPECT_TRUE(bp->nodes.at("n1").params.empty());
}

TEST(BlueprintV2EdgeCase, NodeWithNoPos) {
    auto bp = parse_blueprint_v2(R"({
        "version": 2, "meta": {"name": ""},
        "nodes": { "n1": {"type": "Bus"} }
    })");
    ASSERT_TRUE(bp.has_value());
    EXPECT_FLOAT_EQ(bp->nodes.at("n1").pos[0], 0.0f);
    EXPECT_FLOAT_EQ(bp->nodes.at("n1").pos[1], 0.0f);
}

TEST(BlueprintV2EdgeCase, WireWithEmptyRouting) {
    auto bp = parse_blueprint_v2(R"({
        "version": 2, "meta": {"name": ""},
        "wires": [{"id": "w1", "from": ["a", "p1"], "to": ["b", "p2"], "routing": []}]
    })");
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->wires.size(), 1);
    EXPECT_TRUE(bp->wires[0].routing.empty());
}

TEST(BlueprintV2EdgeCase, WireWithNoRouting) {
    auto bp = parse_blueprint_v2(R"({
        "version": 2, "meta": {"name": ""},
        "wires": [{"id": "w1", "from": ["a", "p1"], "to": ["b", "p2"]}]
    })");
    ASSERT_TRUE(bp.has_value());
    EXPECT_TRUE(bp->wires[0].routing.empty());
}

TEST(BlueprintV2EdgeCase, SubBlueprintReferenceNoOverrides) {
    auto bp = parse_blueprint_v2(R"({
        "version": 2, "meta": {"name": ""},
        "sub_blueprints": {
            "lamp_1": {
                "template": "library/systems/lamp_pass_through",
                "pos": [100, 200]
            }
        }
    })");
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->sub_blueprints.size(), 1);
    const auto& sb = bp->sub_blueprints.at("lamp_1");
    EXPECT_FALSE(sb.is_embedded());
    EXPECT_FALSE(sb.overrides.has_value());
}

TEST(BlueprintV2EdgeCase, SubBlueprintEmptyOverrides) {
    auto bp = parse_blueprint_v2(R"({
        "version": 2, "meta": {"name": ""},
        "sub_blueprints": {
            "lamp_1": {
                "template": "library/systems/lamp_pass_through",
                "pos": [100, 200],
                "overrides": {}
            }
        }
    })");
    ASSERT_TRUE(bp.has_value());
    const auto& sb = bp->sub_blueprints.at("lamp_1");
    ASSERT_TRUE(sb.overrides.has_value());
    EXPECT_TRUE(sb.overrides->params.empty());
    EXPECT_TRUE(sb.overrides->layout.empty());
    EXPECT_TRUE(sb.overrides->routing.empty());
}

TEST(BlueprintV2EdgeCase, EmbeddedSubBlueprintDetected) {
    auto bp = parse_blueprint_v2(R"({
        "version": 2, "meta": {"name": ""},
        "sub_blueprints": {
            "grp1": {
                "pos": [0, 0], "size": [200, 150],
                "nodes": {
                    "a": {"type": "Battery", "pos": [10, 10]}
                },
                "wires": []
            }
        }
    })");
    ASSERT_TRUE(bp.has_value());
    const auto& sb = bp->sub_blueprints.at("grp1");
    EXPECT_TRUE(sb.is_embedded());
    ASSERT_EQ(sb.nodes.size(), 1);
    EXPECT_EQ(sb.nodes.at("a").type, "Battery");
}

TEST(BlueprintV2EdgeCase, MultipleRoutingPoints) {
    auto bp = parse_blueprint_v2(R"({
        "version": 2, "meta": {"name": ""},
        "wires": [{
            "id": "w1", "from": ["a", "out"], "to": ["b", "in"],
            "routing": [[100, 200], [300, 400], [500, 600]]
        }]
    })");
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->wires[0].routing.size(), 3);
    EXPECT_FLOAT_EQ(bp->wires[0].routing[2][0], 500.0f);
}

// ==================================================================
// Regression Tests
// ==================================================================

TEST(BlueprintV2Regression, OverrideKeysAreUnprefixed) {
    auto bp = parse_blueprint_v2(R"({
        "version": 2, "meta": {"name": ""},
        "sub_blueprints": {
            "lamp_1": {
                "template": "library/systems/lamp_pass_through",
                "pos": [400, 300], "size": [200, 150],
                "overrides": {
                    "params": {"lamp.color": "green"},
                    "layout": {"vin": [50, 100], "lamp": [250, 100]},
                    "routing": {"w_in": [[100, 110]]}
                }
            }
        }
    })");
    ASSERT_TRUE(bp.has_value());
    const auto& ov = *bp->sub_blueprints.at("lamp_1").overrides;

    EXPECT_TRUE(ov.layout.count("vin"));
    EXPECT_TRUE(ov.layout.count("lamp"));
    EXPECT_FALSE(ov.layout.count("lamp_1:vin"));
    EXPECT_FALSE(ov.layout.count("lamp_1:lamp"));

    EXPECT_TRUE(ov.routing.count("w_in"));
    EXPECT_FALSE(ov.routing.count("lamp_1:w_in"));
}

TEST(BlueprintV2Regression, OverrideKeysRoundtrip) {
    BlueprintV2 bp;
    bp.version = 2;
    bp.meta.name = "override_key_test";

    SubBlueprintV2 sb;
    sb.template_path = "library/systems/lamp_pass_through";
    sb.pos = {400.0f, 300.0f};
    sb.size = {200.0f, 150.0f};
    OverridesV2 ov;
    ov.layout["vin"] = {50.0f, 100.0f};
    ov.layout["lamp"] = {250.0f, 100.0f};
    ov.routing["w_in"] = {{100.0f, 110.0f}};
    sb.overrides = ov;
    bp.sub_blueprints["lamp_1"] = sb;

    std::string json = serialize_blueprint_v2(bp);
    auto parsed = parse_blueprint_v2(json);
    ASSERT_TRUE(parsed.has_value());

    const auto& pov = *parsed->sub_blueprints.at("lamp_1").overrides;
    EXPECT_FLOAT_EQ(pov.layout.at("vin")[0], 50.0f);
    EXPECT_FLOAT_EQ(pov.layout.at("lamp")[0], 250.0f);
    EXPECT_EQ(pov.routing.at("w_in").size(), 1);
}

TEST(BlueprintV2Regression, ContentKindIsLowercase) {
    BlueprintV2 bp;
    bp.version = 2;
    bp.meta.name = "kind_case_test";
    NodeV2 node;
    node.type = "Voltmeter";
    node.content = ContentV2{"gauge", "V", 0.0f, 0.0f, 30.0f, "V", false};
    bp.nodes["vm"] = node;

    std::string json = serialize_blueprint_v2(bp);
    auto j = nlohmann::json::parse(json);
    EXPECT_EQ(j["nodes"]["vm"]["content"]["kind"], "gauge");

    auto parsed = parse_blueprint_v2(json);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->nodes.at("vm").content->kind, "gauge");
}

TEST(BlueprintV2Regression, MalformedColorIgnored) {
    auto bp = parse_blueprint_v2(R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "n1": {"type": "Bus", "pos": [0, 0], "color": "red"},
            "n2": {"type": "Bus", "pos": [0, 0], "color": null},
            "n3": {"type": "Bus", "pos": [0, 0]}
        }
    })");
    ASSERT_TRUE(bp.has_value());
    EXPECT_FALSE(bp->nodes.at("n1").color.has_value()) << "String color should be ignored";
    EXPECT_FALSE(bp->nodes.at("n2").color.has_value()) << "Null color should be ignored";
    EXPECT_FALSE(bp->nodes.at("n3").color.has_value()) << "Missing color should be nullopt";
}
