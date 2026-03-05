#include <gtest/gtest.h>
#include "editor/persist.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include <set>

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
            {"name": "batt", "classname": "Battery", "ports": {"v_in": {"direction": "In"}, "v_out": {"direction": "Out"}}},
            {"name": "load", "classname": "Resistor", "ports": {"v_in": {"direction": "In"}}}
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

/// Test auto-layout: loading JSON without editor block produces valid positions
TEST(PersistTest, AutoLayout_NoEditorBlock) {
    // Minimal JSON with devices + connections, no "editor" block
    const char* json = R"({
        "devices": [
            { "name": "gnd", "classname": "RefNode",
              "ports": { "v": { "direction": "Out" } },
              "explicit_domains": ["Electrical"] },
            { "name": "bat1", "classname": "Battery",
              "ports": { "v_in": { "direction": "In" }, "v_out": { "direction": "Out" } },
              "explicit_domains": ["Electrical"] },
            { "name": "bus1", "classname": "Bus",
              "ports": { "v": { "direction": "InOut" } },
              "explicit_domains": ["Electrical"] },
            { "name": "load1", "classname": "Load",
              "ports": { "input": { "direction": "In" } },
              "explicit_domains": ["Electrical"] }
        ],
        "connections": [
            { "from": "gnd.v", "to": "bat1.v_in" },
            { "from": "bat1.v_out", "to": "bus1.v" },
            { "from": "bus1.v", "to": "load1.input" }
        ]
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    EXPECT_EQ(bp->nodes.size(), 4);
    EXPECT_EQ(bp->wires.size(), 3);

    // Bus/Ref should have small sizes (40x40), not the default 120x80
    auto* bus = bp->find_node("bus1");
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->kind, NodeKind::Bus);
    EXPECT_LT(bus->size.y, 80.0f);  // bus height should be small

    auto* gnd = bp->find_node("gnd");
    ASSERT_NE(gnd, nullptr);
    EXPECT_EQ(gnd->kind, NodeKind::Ref);

    // All nodes should have distinct positions (not all at origin)
    std::set<std::pair<float, float>> positions;
    for (const auto& n : bp->nodes) {
        positions.insert({n.pos.x, n.pos.y});
    }
    EXPECT_EQ(positions.size(), bp->nodes.size()) << "All nodes should have unique positions";

    // Wires should have routing points (auto-routed)
    int routed = 0;
    for (const auto& w : bp->wires) {
        if (!w.routing_points.empty()) routed++;
    }
    EXPECT_GT(routed, 0) << "At least some wires should have auto-routed paths";
}

/// Test auto-layout with the real composite test file (no editor block)
TEST(PersistTest, AutoLayout_CompositeTestFile) {
    auto bp = load_blueprint_from_file("/Users/vladimir/an24_cpp/an24_composite_test.json");
    ASSERT_TRUE(bp.has_value());
    EXPECT_EQ(bp->nodes.size(), 9);
    EXPECT_EQ(bp->wires.size(), 10);

    // All nodes should have unique positions
    std::set<std::pair<float, float>> positions;
    for (const auto& n : bp->nodes) {
        positions.insert({n.pos.x, n.pos.y});
    }
    EXPECT_EQ(positions.size(), bp->nodes.size());

    // Buses should be in the Bus column (center), sources in source column (left)
    auto* bat = bp->find_node("bat_main_1");
    auto* bus = bp->find_node("dc_bus_1");
    ASSERT_NE(bat, nullptr);
    ASSERT_NE(bus, nullptr);
    EXPECT_LT(bat->pos.x, bus->pos.x) << "Battery should be left of bus";
}

// ─── NodeKind persistence tests ───

TEST(PersistTest, NodeKind_Roundtrip_Bus) {
    Blueprint bp;
    Node n;
    n.id = "mybus";
    n.name = "Custom Bus";
    n.type_name = "Bus"; // Not "Bus" — user-chosen type name
    n.kind = NodeKind::Bus;
    n.output("v");
    n.at(100, 100);
    n.size_wh(40, 40);
    bp.add_node(std::move(n));

    std::string json = blueprint_to_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);
    EXPECT_EQ(bp2->nodes[0].kind, NodeKind::Bus)
        << "Kind should be preserved via 'kind' field, not derived from type_name";
}

TEST(PersistTest, NodeKind_Roundtrip_Ref) {
    Blueprint bp;
    Node n;
    n.id = "myref";
    n.name = "Custom Ref";
    n.type_name = "RefNode"; // Not "RefNode" — user-chosen type name
    n.kind = NodeKind::Ref;
    n.output("v");
    n.at(50, 50);
    n.size_wh(40, 40);
    bp.add_node(std::move(n));

    std::string json = blueprint_to_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);
    EXPECT_EQ(bp2->nodes[0].kind, NodeKind::Ref)
        << "Kind should be preserved via 'kind' field, not derived from type_name";
}

TEST(PersistTest, NodeKind_Roundtrip_Node) {
    Blueprint bp;
    Node n;
    n.id = "comp1";
    n.name = "Some Component";
    n.type_name = "Battery";
    n.kind = NodeKind::Node;
    n.input("v_in");
    n.output("v_out");
    n.at(200, 200);
    n.size_wh(120, 80);
    bp.add_node(std::move(n));

    std::string json = blueprint_to_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);
    EXPECT_EQ(bp2->nodes[0].kind, NodeKind::Node);
}

TEST(PersistTest, NodeKind_BackwardCompat_NoKindField) {
    // Old JSON without "kind" field — should fall back to type_name matching
    const char* json = R"({
        "devices": [
            {"name": "b", "classname": "Bus", "ports": {"v": {"direction": "Out"}}},
            {"name": "r", "classname": "RefNode", "ports": {"v": {"direction": "Out"}}},
            {"name": "batt", "classname": "Battery", "ports": {"v_in": {"direction": "In"}}}
        ],
        "connections": []
    })";
    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 3);
    EXPECT_EQ(bp->nodes[0].kind, NodeKind::Bus);
    EXPECT_EQ(bp->nodes[1].kind, NodeKind::Ref);
    EXPECT_EQ(bp->nodes[2].kind, NodeKind::Node);
}

TEST(PersistTest, RefNode_ValueByKind_NotTypeName) {
    // A Ref node with a non-standard type_name should still get value=0.0
    Blueprint bp;
    Node n;
    n.id = "gnd";
    n.name = "Ground";
    n.type_name = "CustomGround"; // NOT "RefNode"
    n.kind = NodeKind::Ref;
    n.output("v");
    bp.add_node(std::move(n));

    std::string json = blueprint_to_json(bp);
    // value=0.0 should be set based on kind=Ref, not type_name
    EXPECT_NE(json.find("\"value\": \"0.0\""), std::string::npos)
        << "RefNode value should be set by kind, not type_name";
}

// [e5f6] node_content params should survive roundtrip
