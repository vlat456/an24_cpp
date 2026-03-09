#include <gtest/gtest.h>
#include "editor/visual/scene/persist.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/app.h"
#include "json_parser/json_parser.h"
#include <nlohmann/json.hpp>

using namespace an24;
using json = nlohmann::json;

// =============================================================================
// Phase 0: Params Data Integrity — TDD tests
// =============================================================================

// --- 0.1 add_component() should populate default params ---

TEST(ParamsIntegrity, AddComponentPopulatesDefaultParams) {
    EditorApp app;

    // Battery has 6 default params in components/Battery.json
    ASSERT_TRUE(app.type_registry.has("Battery"));
    const auto* def = app.type_registry.get("Battery");
    ASSERT_NE(def, nullptr);
    ASSERT_FALSE(def->params.empty());

    app.add_component("Battery", Pt(100, 100));

    // Find the newly added battery node
    ASSERT_EQ(app.blueprint.nodes.size(), 1);
    const Node& node = app.blueprint.nodes[0];

    // Node params must contain ALL default params
    for (const auto& [key, value] : def->params) {
        EXPECT_TRUE(node.params.count(key) > 0)
            << "Missing param '" << key << "' in node.params after add_component()";
        EXPECT_EQ(node.params.at(key), value)
            << "Param '" << key << "' should be '" << value
            << "' but got '" << node.params.at(key) << "'";
    }
}

TEST(ParamsIntegrity, AddComponentPopulatesLerpNodeParams) {
    EditorApp app;

    // LerpNode has 1 default param: factor=0.05
    ASSERT_TRUE(app.type_registry.has("LerpNode"));

    app.add_component("LerpNode", Pt(200, 200));

    ASSERT_EQ(app.blueprint.nodes.size(), 1);
    const Node& node = app.blueprint.nodes[0];

    EXPECT_EQ(node.params.at("factor"), "0.05");
}

TEST(ParamsIntegrity, AddComponentNoParamsForUnknownComponent) {
    EditorApp app;

    // Adding an unknown component should not crash
    app.add_component("NonexistentWidget", Pt(100, 100));

    // No node should be added (error handled in add_component)
    EXPECT_EQ(app.blueprint.nodes.size(), 0);
}

// --- 0.2 load_editor_format() should merge params with registry ---

TEST(ParamsIntegrity, LoadedBlueprintHasFullParams) {
    // JSON with a Battery that has NO params saved (simulating old format)
    const char* json_str = R"({
        "devices": [
            {
                "name": "batt1",
                "classname": "Battery",
                "kind": "Node",
                "ports": {
                    "v_in": {"direction": "In", "type": "V"},
                    "v_out": {"direction": "Out", "type": "V"}
                },
                "pos": {"x": 100, "y": 100},
                "size": {"x": 120, "y": 80}
            }
        ],
        "wires": []
    })";

    auto bp = blueprint_from_json(json_str);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1);

    const Node& node = bp->nodes[0];

    // After loading, params should be filled from registry defaults
    TypeRegistry registry = load_type_registry("library/");
    const auto* def = registry.get("Battery");
    ASSERT_NE(def, nullptr);

    for (const auto& [key, value] : def->params) {
        EXPECT_TRUE(node.params.count(key) > 0)
            << "Missing param '" << key << "' after load (no params in JSON)";
        EXPECT_EQ(node.params.at(key), value)
            << "Param '" << key << "' should default to '" << value << "'";
    }
}

TEST(ParamsIntegrity, UserOverridesPreservedOnLoad) {
    // JSON with a Battery that has partial params (user override)
    const char* json_str = R"({
        "devices": [
            {
                "name": "batt1",
                "classname": "Battery",
                "kind": "Node",
                "ports": {
                    "v_in": {"direction": "In", "type": "V"},
                    "v_out": {"direction": "Out", "type": "V"}
                },
                "params": {
                    "v_nominal": "24.0"
                },
                "pos": {"x": 100, "y": 100},
                "size": {"x": 120, "y": 80}
            }
        ],
        "wires": []
    })";

    auto bp = blueprint_from_json(json_str);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1);

    const Node& node = bp->nodes[0];

    // User override must be preserved
    EXPECT_EQ(node.params.at("v_nominal"), "24.0");

    // Missing defaults must be filled
    EXPECT_EQ(node.params.at("internal_r"), "0.01");
    EXPECT_EQ(node.params.at("inv_internal_r"), "100.0");
    EXPECT_EQ(node.params.at("capacity"), "1000.0");
    EXPECT_EQ(node.params.at("inv_capacity"), "0.001");
    EXPECT_EQ(node.params.at("charge"), "1000.0");
}

TEST(ParamsIntegrity, SavedParamsRoundtrip) {
    // Create a blueprint with a Battery with modified params
    Blueprint bp;
    Node n;
    n.id = "batt1";
    n.name = "batt1";
    n.type_name = "Battery";
    n.pos = Pt(100, 100);
    n.size = Pt(120, 80);
    n.input("v_in");
    n.output("v_out");
    n.params = {
        {"v_nominal", "24.0"},
        {"internal_r", "0.05"},
        {"inv_internal_r", "20.0"},
        {"capacity", "500.0"},
        {"inv_capacity", "0.002"},
        {"charge", "500.0"}
    };
    bp.add_node(std::move(n));

    // Save → Load roundtrip
    std::string saved_json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(saved_json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);

    const Node& loaded = bp2->nodes[0];

    // All params must survive roundtrip exactly
    EXPECT_EQ(loaded.params.at("v_nominal"), "24.0");
    EXPECT_EQ(loaded.params.at("internal_r"), "0.05");
    EXPECT_EQ(loaded.params.at("inv_internal_r"), "20.0");
    EXPECT_EQ(loaded.params.at("capacity"), "500.0");
    EXPECT_EQ(loaded.params.at("inv_capacity"), "0.002");
    EXPECT_EQ(loaded.params.at("charge"), "500.0");
}

TEST(ParamsIntegrity, ComponentWithNoDefaultParams_StaysEmpty) {
    // If a component type has no params, node.params should remain empty
    // (e.g., Bus has no params)
    const char* json_str = R"({
        "devices": [
            {
                "name": "bus1",
                "classname": "Bus",
                "kind": "Bus",
                "ports": {
                    "a": {"direction": "InOut", "type": "V"}
                },
                "pos": {"x": 100, "y": 100},
                "size": {"x": 40, "y": 40}
            }
        ],
        "wires": []
    })";

    auto bp = blueprint_from_json(json_str);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1);

    TypeRegistry registry = load_type_registry("library/");
    const auto* def = registry.get("Bus");
    ASSERT_NE(def, nullptr);

    // Bus has no default params
    if (def->params.empty()) {
        EXPECT_TRUE(bp->nodes[0].params.empty());
    }
}

// =============================================================================
// Phase 4: add_component() uses cpp_class to set expandable
// =============================================================================

TEST(ParamsIntegrity, AddComponent_CppClassTrue_GetsInternalCPP) {
    EditorApp app;
    // Battery is cpp_class=true in library/
    ASSERT_TRUE(app.type_registry.has("Battery"));
    EXPECT_TRUE(app.type_registry.get("Battery")->cpp_class);

    app.add_component("Battery", Pt(100, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);
    EXPECT_FALSE(app.blueprint.nodes[0].expandable)
        << "cpp_class=true components must not be expandable";
}

TEST(ParamsIntegrity, AddComponent_CppClassFalse_GetsBlueprint) {
    EditorApp app;
    // simple_battery is cpp_class=false in library/
    ASSERT_TRUE(app.type_registry.has("simple_battery"));
    EXPECT_FALSE(app.type_registry.get("simple_battery")->cpp_class);

    app.add_component("simple_battery", Pt(200, 200));
    // Blueprint expansion: internal devices + 1 collapsed node
    ASSERT_GE(app.blueprint.nodes.size(), 2u);
    // Find the collapsed node (type_name matches the blueprint)
    bool found_collapsed = false;
    for (const auto& n : app.blueprint.nodes) {
        if (n.type_name == "simple_battery" && n.expandable) {
            found_collapsed = true;
            break;
        }
    }
    EXPECT_TRUE(found_collapsed)
        << "cpp_class=false types must create an expandable collapsed node";
}

TEST(ParamsIntegrity, AddComponent_BusStillGetsBusKind) {
    EditorApp app;
    app.add_component("Bus", Pt(100, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);
    EXPECT_EQ(app.blueprint.nodes[0].render_hint, "bus");
}

TEST(ParamsIntegrity, AddComponent_RefNodeStillGetsRefKind) {
    EditorApp app;
    app.add_component("RefNode", Pt(100, 100));
    ASSERT_EQ(app.blueprint.nodes.size(), 1);
    EXPECT_EQ(app.blueprint.nodes[0].render_hint, "ref");
}
