#include <gtest/gtest.h>
#include "editor/persist.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"

/// TDD Step 2: Persist - сначала тесты

TEST(PersistTest, ToJson_EmptyBlueprint) {
    Blueprint bp;
    std::string json = blueprint_to_json(bp);
    EXPECT_FALSE(json.empty());
    // Unified format: devices (simulator format) + editor section
    EXPECT_NE(json.find("devices"), std::string::npos);
    EXPECT_NE(json.find("editor"), std::string::npos);
}

TEST(PersistTest, ToJson_FromJson_Roundtrip) {
    Blueprint bp;

    // Добавим узел
    Node n;
    n.id = "batt1";
    n.name = "Battery 28V";
    n.type_name = "Battery";
    n.at(100.0f, 200.0f);
    n.input("v_in");
    n.output("v_out");
    bp.add_node(std::move(n));

    // Добавим второй узел (чтобы провод имел валидный endpoint)
    Node n2;
    n2.id = "load1";
    n2.name = "Load";
    n2.type_name = "Resistor";
    n2.at(300.0f, 200.0f);
    n2.input("v_in");
    bp.add_node(std::move(n2));

    // Добавим провод
    Wire w;
    w.id = "wire1";
    w.start.node_id = "batt1";
    w.start.port_name = "v_out";
    w.end.node_id = "load1";
    w.end.port_name = "v_in";
    bp.add_wire(std::move(w));

    // Roundtrip: to JSON -> from JSON
    std::string json = blueprint_to_json(bp);
    auto bp2 = blueprint_from_json(json);

    ASSERT_TRUE(bp2.has_value());
    EXPECT_EQ(bp2->nodes.size(), 2);
    EXPECT_EQ(bp2->nodes[0].id, "batt1");
    EXPECT_EQ(bp2->nodes[0].pos.x, 100.0f);
    EXPECT_EQ(bp2->nodes[0].pos.y, 200.0f);
    EXPECT_EQ(bp2->nodes[0].inputs.size(), 1);
    EXPECT_EQ(bp2->wires.size(), 1);
    EXPECT_EQ(bp2->wires[0].start.node_id, "batt1");
}

TEST(PersistTest, FromJson_Invalid_ReturnsNullopt) {
    auto bp = blueprint_from_json("invalid json {");
    EXPECT_FALSE(bp.has_value());
}

TEST(PersistTest, ToJson_IncludesViewport) {
    Blueprint bp;
    bp.pan = Pt(50.0f, 75.0f);
    bp.zoom = 1.5f;
    bp.grid_step = 24.0f;

    std::string json = blueprint_to_json(bp);
    auto bp2 = blueprint_from_json(json);

    ASSERT_TRUE(bp2.has_value());
    EXPECT_EQ(bp2->pan.x, 50.0f);
    EXPECT_EQ(bp2->zoom, 1.5f);
    EXPECT_EQ(bp2->grid_step, 24.0f);
}

/// Test unified format: simulator format + editor metadata
TEST(PersistTest, UnifiedFormat_WithEditorMetadata) {
    // Simulator format with editor section
    const char* json = R"({
        "devices": [
            {"name": "batt", "internal": "Battery", "ports": {"v_in": {"direction": "In"}, "v_out": {"direction": "Out"}}},
            {"name": "load", "internal": "Resistor", "ports": {"v_in": {"direction": "In"}}}
        ],
        "connections": [
            {"from": "batt.v_out", "to": "load.v_in"}
        ],
        "editor": {
            "viewport": {"pan": {"x": 100, "y": 200}, "zoom": 2.0, "grid_step": 32},
            "nodes": {
                "batt": {"pos": {"x": 50, "y": 60}, "size": {"x": 120, "y": 80}},
                "load": {"pos": {"x": 250, "y": 60}, "size": {"x": 100, "y": 60}}
            },
            "wires": [
                {"from": "batt.v_out", "to": "load.v_in", "routing_points": [{"x": 150, "y": 60}, {"x": 150, "y": 100}]}
            ]
        }
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());

    // Check devices converted to nodes
    EXPECT_EQ(bp->nodes.size(), 2);

    // Check editor viewport
    EXPECT_EQ(bp->pan.x, 100.0f);
    EXPECT_EQ(bp->pan.y, 200.0f);
    EXPECT_EQ(bp->zoom, 2.0f);
    EXPECT_EQ(bp->grid_step, 32.0f);

    // Check node visual states from editor section
    auto* batt = bp->find_node("batt");
    ASSERT_NE(batt, nullptr);
    EXPECT_EQ(batt->pos.x, 50.0f);
    EXPECT_EQ(batt->pos.y, 60.0f);
    EXPECT_EQ(batt->size.x, 120.0f);

    // Check wire routing points
    ASSERT_EQ(bp->wires.size(), 1);
    EXPECT_EQ(bp->wires[0].routing_points.size(), 2);
    EXPECT_EQ(bp->wires[0].routing_points[0].x, 150.0f);
}

/// Test invalid format returns nullopt
TEST(PersistTest, FromJson_MissingDevices_ReturnsNullopt) {
    const char* json = R"({
        "connections": []
    })";
    auto bp = blueprint_from_json(json);
    EXPECT_FALSE(bp.has_value());
}

/// Test loading vsu_test.json (simulator format)
TEST(PersistTest, LoadSimulatorFormat_VsuTest) {
    auto bp = load_blueprint_from_file("/Users/vladimir/an24_cpp/src/aircraft/vsu_test.json");
    ASSERT_TRUE(bp.has_value());
    EXPECT_EQ(bp->nodes.size(), 6);
    EXPECT_EQ(bp->wires.size(), 8);
    // Check specific devices
    auto* gnd = bp->find_node("gnd");
    ASSERT_NE(gnd, nullptr);
    EXPECT_EQ(gnd->type_name, "RefNode");
}
