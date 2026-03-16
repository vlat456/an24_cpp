#include <gtest/gtest.h>
#include "editor/visual/persist.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/document.h"
#include "json_parser/json_parser.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Helper fixture to set up TypeRegistry and Document for each test
class ParamsIntegrityTest : public ::testing::Test {
protected:
    TypeRegistry registry;
    Document doc;

    void SetUp() override {
        registry = load_type_registry("library/");
    }
};

// =============================================================================
// Phase 0: Params Data Integrity — TDD tests
// =============================================================================

// --- 0.1 add_component() should populate default params ---

TEST_F(ParamsIntegrityTest, AddComponentPopulatesDefaultParams) {
    // Battery has 6 default params in components/Battery.json
    ASSERT_TRUE(registry.has("Battery"));
    const auto* def = registry.get("Battery");
    ASSERT_NE(def, nullptr);
    ASSERT_FALSE(def->params.empty());

    doc.addComponent("Battery", Pt(100, 100), "", registry);

    // Find the newly added battery node
    ASSERT_EQ(doc.blueprint().nodes.size(), 1);
    const Node& node = doc.blueprint().nodes[0];

    // Node params must contain ALL default params
    for (const auto& [key, value] : def->params) {
        EXPECT_TRUE(node.params.count(key) > 0)
            << "Missing param '" << key << "' in node.params after addComponent()";
        EXPECT_EQ(node.params.at(key), value)
            << "Param '" << key << "' should be '" << value
            << "' but got '" << node.params.at(key) << "'";
    }
}

TEST_F(ParamsIntegrityTest, AddComponentPopulatesLerpNodeParams) {
    // LerpNode has 1 default param: factor=0.05
    ASSERT_TRUE(registry.has("LerpNode"));

    doc.addComponent("LerpNode", Pt(200, 200), "", registry);

    ASSERT_EQ(doc.blueprint().nodes.size(), 1);
    const Node& node = doc.blueprint().nodes[0];

    EXPECT_EQ(node.params.at("factor"), "0.05");
}

TEST_F(ParamsIntegrityTest, AddComponentNoParamsForUnknownComponent) {
    // Adding an unknown component should not crash
    doc.addComponent("NonexistentWidget", Pt(100, 100), "", registry);

    // No node should be added (error handled in addComponent)
    EXPECT_EQ(doc.blueprint().nodes.size(), 0);
}

// --- 0.2 load_editor_format() should merge params with registry ---

TEST_F(ParamsIntegrityTest, LoadedBlueprintHasFullParams) {
    // JSON with a Battery that has NO params saved (simulating old format)
    const char* json_str = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "batt1": {
                "type": "Battery",
                "pos": [100, 100],
                "size": [120, 80]
            }
        },
        "wires": []
    })";

    auto bp = Blueprint::deserialize(json_str);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1);

    const Node& node = bp->nodes[0];

    // After loading, params should be filled from registry defaults
    const auto* def = registry.get("Battery");
    ASSERT_NE(def, nullptr);

    for (const auto& [key, value] : def->params) {
        EXPECT_TRUE(node.params.count(key) > 0)
            << "Missing param '" << key << "' after load (no params in JSON)";
        EXPECT_EQ(node.params.at(key), value)
            << "Param '" << key << "' should default to '" << value << "'";
    }
}

TEST_F(ParamsIntegrityTest, UserOverridesPreservedOnLoad) {
    // JSON with a Battery that has partial params (user override)
    const char* json_str = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "batt1": {
                "type": "Battery",
                "params": {"v_nominal": "24.0"},
                "pos": [100, 100],
                "size": [120, 80]
            }
        },
        "wires": []
    })";

    auto bp = Blueprint::deserialize(json_str);
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

TEST_F(ParamsIntegrityTest, SavedParamsRoundtrip) {
    // Create a blueprint with a Battery with modified params
    Blueprint bp;
    auto& I = bp.interner();
    Node n;
    n.id = I.intern("batt1");
    n.name = "batt1";
    n.type_name = "Battery";
    n.pos = Pt(100, 100);
    n.set_explicit_size(Pt(120, 80));
    n.input(I.intern("v_in"));
    n.output(I.intern("v_out"));
    n.params = {
        {"v_nominal", "24.0"},
        {"internal_r", "0.05"},
        {"inv_internal_r", "20.0"},
        {"capacity", "500.0"},
        {"inv_capacity", "0.002"},
        {"charge", "500.0"}
    };
    bp.add_node(std::move(n));

    // Save -> Load roundtrip
    std::string saved_json = bp.serialize();
    auto bp2 = Blueprint::deserialize(saved_json);
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

TEST_F(ParamsIntegrityTest, ComponentWithNoDefaultParams_StaysEmpty) {
    // If a component type has no params, node.params should remain empty
    // (e.g., Bus has no params)
    const char* json_str = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "bus1": {
                "type": "Bus",
                "pos": [100, 100],
                "size": [40, 40]
            }
        },
        "wires": []
    })";

    auto bp = Blueprint::deserialize(json_str);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1);

    const auto* def = registry.get("Bus");
    ASSERT_NE(def, nullptr);

    // Bus has no default params
    if (def->params.empty()) {
        EXPECT_TRUE(bp->nodes[0].params.empty());
    }
}

// =============================================================================
// Phase 4: addComponent() uses cpp_class to set expandable
// =============================================================================

TEST_F(ParamsIntegrityTest, AddComponent_CppClassTrue_GetsInternalCPP) {
    // Battery is cpp_class=true in library/
    ASSERT_TRUE(registry.has("Battery"));
    EXPECT_TRUE(registry.get("Battery")->cpp_class);

    doc.addComponent("Battery", Pt(100, 100), "", registry);
    ASSERT_EQ(doc.blueprint().nodes.size(), 1);
    EXPECT_FALSE(doc.blueprint().nodes[0].expandable)
        << "cpp_class=true components must not be expandable";
}

TEST_F(ParamsIntegrityTest, AddComponent_CppClassFalse_GetsBlueprint) {
    // simple_battery is cpp_class=false in library/
    ASSERT_TRUE(registry.has("simple_battery"));
    EXPECT_FALSE(registry.get("simple_battery")->cpp_class);

    doc.addComponent("simple_battery", Pt(200, 200), "", registry);
    // Blueprint expansion: internal devices + 1 collapsed node
    ASSERT_GE(doc.blueprint().nodes.size(), 2u);
    // Find the collapsed node (type_name matches the blueprint)
    bool found_collapsed = false;
    for (const auto& n : doc.blueprint().nodes) {
        if (n.type_name == "simple_battery" && n.expandable) {
            found_collapsed = true;
            break;
        }
    }
    EXPECT_TRUE(found_collapsed)
        << "cpp_class=false types must create an expandable collapsed node";
}

TEST_F(ParamsIntegrityTest, AddComponent_BusStillGetsBusKind) {
    doc.addComponent("Bus", Pt(100, 100), "", registry);
    ASSERT_EQ(doc.blueprint().nodes.size(), 1);
    EXPECT_EQ(doc.blueprint().nodes[0].render_hint, "bus");
}

TEST_F(ParamsIntegrityTest, AddComponent_RefNodeStillGetsRefKind) {
    doc.addComponent("RefNode", Pt(100, 100), "", registry);
    ASSERT_EQ(doc.blueprint().nodes.size(), 1);
    EXPECT_EQ(doc.blueprint().nodes[0].render_hint, "ref");
}
