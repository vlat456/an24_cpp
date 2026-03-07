#include <gtest/gtest.h>
#include "editor/visual/scene/persist.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "editor/visual/node/node.h"
#include "json_parser/json_parser.h"
#include <nlohmann/json.hpp>
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
    // classname (type_name) is the single source of truth for C++ binding.
    // A node with type_name="RefNode" should get value=0.0 fallback
    // regardless of its NodeKind (kind is for visual only).
    Blueprint bp;
    Node n;
    n.id = "gnd";
    n.name = "Ground";
    n.type_name = "RefNode";
    n.kind = NodeKind::Node;  // kind doesn't matter — classname does
    n.output("v");
    bp.add_node(std::move(n));

    std::string json = blueprint_to_json(bp);
    // value=0.0 should be set based on type_name="RefNode", not kind
    EXPECT_NE(json.find("\"value\": \"0.0\""), std::string::npos)
        << "RefNode value should be set by classname (type_name), not kind";
}

// =============================================================================
// Tests for get_default_node_size() - single source of truth for node sizing
// =============================================================================

TEST(PersistTest, GetDefaultNodeSize_Bus_Returns40x40) {
    // Bus has default_size {2, 2} in JSON = 40x40 pixels
    using namespace an24;
    ComponentRegistry registry = load_component_registry("components/");

    Pt size = get_default_node_size("Bus", &registry);
    EXPECT_FLOAT_EQ(size.x, 40.0f);
    EXPECT_FLOAT_EQ(size.y, 40.0f);
}

TEST(PersistTest, GetDefaultNodeSize_RefNode_Returns40x40) {
    // RefNode has default_size {2, 2} in JSON = 40x40 pixels
    using namespace an24;
    ComponentRegistry registry = load_component_registry("components/");

    Pt size = get_default_node_size("RefNode", &registry);
    EXPECT_FLOAT_EQ(size.x, 40.0f);
    EXPECT_FLOAT_EQ(size.y, 40.0f);
}

TEST(PersistTest, GetDefaultNodeSize_Ref_Alias_Returns40x40) {
    // Ref (alias for RefNode) - uses same size through registry
    using namespace an24;
    ComponentRegistry registry = load_component_registry("components/");

    Pt size = get_default_node_size("RefNode", &registry);
    EXPECT_FLOAT_EQ(size.x, 40.0f);
    EXPECT_FLOAT_EQ(size.y, 40.0f);
}

TEST(PersistTest, GetDefaultNodeSize_UnknownComponent_Returns120x80) {
    // Unknown components without registry should return default 120x80
    Pt size = get_default_node_size("UnknownComponent", nullptr);
    EXPECT_FLOAT_EQ(size.x, 120.0f);
    EXPECT_FLOAT_EQ(size.y, 80.0f);
}

TEST(PersistTest, GetDefaultNodeSize_WithRegistry) {
    // Test that default_size from component registry is used correctly
    using namespace an24;

    ComponentRegistry registry;

    // Create a mock component definition with default_size
    ComponentDefinition def;
    def.classname = "TestComponent";
    def.default_size = {2, 3};  // 2x3 grid units = 40x60 pixels

    registry.components["TestComponent"] = def;

    Pt size = get_default_node_size("TestComponent", &registry);
    EXPECT_FLOAT_EQ(size.x, 40.0f);  // 2 * 20
    EXPECT_FLOAT_EQ(size.y, 60.0f);  // 3 * 20
}

TEST(PersistTest, GetDefaultNodeSize_Splitter_Returns60x60) {
    // Splitter has default_size {3, 3} in its JSON definition
    // 3x3 grid units = 60x60 pixels
    using namespace an24;

    // Load real component registry
    ComponentRegistry registry = load_component_registry("components/");

    const auto* split_def = registry.get("Splitter");
    ASSERT_NE(split_def, nullptr) << "Splitter component should exist in registry";
    ASSERT_TRUE(split_def->default_size.has_value()) << "Splitter should have default_size defined";
    EXPECT_EQ(split_def->default_size->first, 3);
    EXPECT_EQ(split_def->default_size->second, 3);

    Pt size = get_default_node_size("Splitter", &registry);
    EXPECT_FLOAT_EQ(size.x, 60.0f) << "Splitter width should be 3 grid units (60px)";
    EXPECT_FLOAT_EQ(size.y, 60.0f) << "Splitter height should be 3 grid units (60px)";
}

TEST(PersistTest, GetDefaultNodeSize_GridUnitConversion) {
    // Test that grid unit conversion is correct: 1 unit = 20 pixels
    using namespace an24;

    ComponentRegistry registry;

    ComponentDefinition def;
    def.classname = "TestComponent";
    def.default_size = {1, 1};  // 1x1 grid unit

    registry.components["TestComponent"] = def;

    Pt size = get_default_node_size("TestComponent", &registry);
    EXPECT_FLOAT_EQ(size.x, 20.0f) << "1 grid unit should be 20 pixels";
    EXPECT_FLOAT_EQ(size.y, 20.0f) << "1 grid unit should be 20 pixels";
}


// [e5f6] node_content params should survive roundtrip

// =============================================================================
// Tests for BusVisualNode dynamic resizing
// =============================================================================

TEST(PersistTest, BusVisualNode_InitialSizeFromComponentDefinition) {
    // Bus has default_size {2, 2} in JSON = 40x40 pixels
    // Visual grid snapping (16px) snaps 40px up to 48px (3 * 16)
    using namespace an24;
    ComponentRegistry registry = load_component_registry("components/");

    Node n;
    n.id = "bus1";
    n.name = "Test Bus";
    n.type_name = "Bus";
    n.kind = NodeKind::Bus;
    n.at(100, 100);
    n.size = get_default_node_size("Bus", &registry);
    n.output("v");

    std::vector<Wire> wires;
    BusVisualNode bus(n, BusOrientation::Horizontal, wires);

    EXPECT_EQ(bus.getSize().x, 48.0f) << "Bus width should be 48px (40px snapped to 16px visual grid)";
    EXPECT_EQ(bus.getSize().y, 48.0f) << "Bus height should be 48px (40px snapped to 16px visual grid)";
}

TEST(PersistTest, BusVisualNode_ResizesWhenWireAdded) {
    // Bus should resize when a wire is connected (port count increases)
    using namespace an24;
    ComponentRegistry registry = load_component_registry("components/");

    // Create bus node
    Node bus_node;
    bus_node.id = "bus1";
    bus_node.name = "Test Bus";
    bus_node.type_name = "Bus";
    bus_node.kind = NodeKind::Bus;
    bus_node.at(100, 100);
    bus_node.size = get_default_node_size("Bus", &registry);
    bus_node.output("v");

    // Create another node to connect to
    Node other_node;
    other_node.id = "other";
    other_node.name = "Other";
    other_node.type_name = "Battery";
    other_node.kind = NodeKind::Node;
    other_node.at(200, 100);
    other_node.output("v_out");

    std::vector<Wire> wires;
    BusVisualNode bus(bus_node, BusOrientation::Horizontal, wires);

    // Initial size (1 port: "v")
    float initial_width = bus.getSize().x;
    EXPECT_GT(initial_width, 0.0f);

    // Add a wire - this should create an alias port and increase size
    Wire w;
    w.id = "wire1";
    w.start.node_id = "other";
    w.start.port_name = "v_out";
    w.end.node_id = "bus1";
    w.end.port_name = "v";

    bus.connectWire(w);

    // Bus should now have 2 ports (alias + "v")
    EXPECT_EQ(bus.getPortCount(), 2);
    // Width should increase (more ports need more space)
    EXPECT_GT(bus.getSize().x, initial_width) << "Bus width should increase after adding wire";
}

TEST(PersistTest, BusVisualNode_ResizesWhenWireRemoved) {
    // Bus should resize when a wire is disconnected (port count decreases)
    using namespace an24;
    ComponentRegistry registry = load_component_registry("components/");

    // Create bus node
    Node bus_node;
    bus_node.id = "bus1";
    bus_node.name = "Test Bus";
    bus_node.type_name = "Bus";
    bus_node.kind = NodeKind::Bus;
    bus_node.at(100, 100);
    bus_node.size = get_default_node_size("Bus", &registry);
    bus_node.output("v");

    // Create other nodes
    Node other1, other2;
    other1.id = "other1";
    other1.name = "Other1";
    other1.type_name = "Battery";
    other1.kind = NodeKind::Node;
    other1.at(200, 100);
    other1.output("v_out");

    other2.id = "other2";
    other2.name = "Other2";
    other2.type_name = "Battery";
    other2.kind = NodeKind::Node;
    other2.at(300, 100);
    other2.output("v_out");

    // Start with 2 wires
    std::vector<Wire> wires;
    Wire w1;
    w1.id = "wire1";
    w1.start.node_id = "other1";
    w1.start.port_name = "v_out";
    w1.end.node_id = "bus1";
    w1.end.port_name = "v";

    Wire w2;
    w2.id = "wire2";
    w2.start.node_id = "other2";
    w2.start.port_name = "v_out";
    w2.end.node_id = "bus1";
    w2.end.port_name = "v";

    wires.push_back(w1);
    wires.push_back(w2);

    BusVisualNode bus(bus_node, BusOrientation::Horizontal, wires);

    // Should have 3 ports (2 aliases + "v")
    EXPECT_EQ(bus.getPortCount(), 3);
    float width_with_2_wires = bus.getSize().x;

    // Remove one wire
    bus.disconnectWire(w1);

    // Should now have 2 ports (1 alias + "v")
    EXPECT_EQ(bus.getPortCount(), 2);
    // Width should decrease
    EXPECT_LT(bus.getSize().x, width_with_2_wires) << "Bus width should decrease after removing wire";
}

TEST(PersistTest, BusVisualNode_SizeIsGridSnapped) {
    // Bus size should always be a multiple of GRID_STEP (16px)
    using namespace an24;
    ComponentRegistry registry = load_component_registry("components/");

    Node n;
    n.id = "bus1";
    n.name = "Test Bus";
    n.type_name = "Bus";
    n.kind = NodeKind::Bus;
    n.at(100, 100);
    n.size = get_default_node_size("Bus", &registry);
    n.output("v");

    std::vector<Wire> wires;
    BusVisualNode bus(n, BusOrientation::Horizontal, wires);

    constexpr float GRID_STEP = 16.0f;

    // Initial size should be grid-snapped
    EXPECT_NEAR(fmod(bus.getSize().x, GRID_STEP), 0.0f, 0.01f) << "Width should be multiple of grid step";
    EXPECT_NEAR(fmod(bus.getSize().y, GRID_STEP), 0.0f, 0.01f) << "Height should be multiple of grid step";

    // After adding wire, size should still be grid-snapped
    Wire w;
    w.id = "wire1";
    w.start.node_id = "other";
    w.start.port_name = "v_out";
    w.end.node_id = "bus1";
    w.end.port_name = "v";

    bus.connectWire(w);

    EXPECT_NEAR(fmod(bus.getSize().x, GRID_STEP), 0.0f, 0.01f) << "Width should remain grid-snapped";
    EXPECT_NEAR(fmod(bus.getSize().y, GRID_STEP), 0.0f, 0.01f) << "Height should remain grid-snapped";
}

// ─── Regression: Port type roundtrip through editor format ─────────────────

TEST(PersistTest, PortType_EditorFormat_Roundtrip) {
    Blueprint bp;

    Node bus;
    bus.id = "main_bus";
    bus.name = "main_bus";
    bus.type_name = "Bus";
    bus.kind = NodeKind::Bus;
    bus.at(100.0f, 100.0f);
    // Bus port is InOut with type V
    Port bus_port;
    bus_port.name = "v";
    bus_port.side = PortSide::InOut;
    bus_port.type = an24::PortType::V;
    bus.inputs.push_back(bus_port);
    bus.outputs.push_back(bus_port);
    bp.add_node(std::move(bus));

    Node dmr;
    dmr.id = "dmr1";
    dmr.name = "dmr1";
    dmr.type_name = "DMR400";
    dmr.at(200.0f, 100.0f);
    Port vin; vin.name = "v_in"; vin.side = PortSide::Input; vin.type = an24::PortType::V;
    Port vout; vout.name = "v_out"; vout.side = PortSide::Output; vout.type = an24::PortType::V;
    Port lamp; lamp.name = "lamp"; lamp.side = PortSide::Output; lamp.type = an24::PortType::V;
    dmr.inputs.push_back(vin);
    dmr.outputs.push_back(vout);
    dmr.outputs.push_back(lamp);
    bp.add_node(std::move(dmr));

    // Roundtrip through editor format
    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());

    // Check Bus port is InOut
    const Node* loaded_bus = bp2->find_node("main_bus");
    ASSERT_NE(loaded_bus, nullptr);
    bool found_in = false, found_out = false;
    for (const auto& p : loaded_bus->inputs)
        if (p.name == "v") found_in = true;
    for (const auto& p : loaded_bus->outputs)
        if (p.name == "v") found_out = true;
    EXPECT_TRUE(found_in) << "Bus 'v' should be in inputs (InOut)";
    EXPECT_TRUE(found_out) << "Bus 'v' should be in outputs (InOut)";

    // Check port types are preserved
    const Node* loaded_dmr = bp2->find_node("dmr1");
    ASSERT_NE(loaded_dmr, nullptr);
    for (const auto& p : loaded_dmr->inputs) {
        if (p.name == "v_in")
            EXPECT_EQ(p.type, an24::PortType::V) << "v_in port type should be V";
    }
    for (const auto& p : loaded_dmr->outputs) {
        if (p.name == "v_out")
            EXPECT_EQ(p.type, an24::PortType::V) << "v_out port type should be V";
        if (p.name == "lamp")
            EXPECT_EQ(p.type, an24::PortType::V) << "lamp port type should be V";
    }
}

// ─── Regression: InOut port allows wire from both directions ───────────────

TEST(PersistTest, InOutPort_AllowsWireFromBothDirections) {
    Blueprint bp;

    // Bus with InOut port
    Node bus;
    bus.id = "bus1";
    bus.name = "bus1";
    bus.type_name = "Bus";
    bus.kind = NodeKind::Bus;
    Port bus_port;
    bus_port.name = "v";
    bus_port.side = PortSide::InOut;
    bus_port.type = an24::PortType::V;
    bus.inputs.push_back(bus_port);
    bus.outputs.push_back(bus_port);
    bp.add_node(std::move(bus));

    // DMR with output
    Node dmr;
    dmr.id = "dmr1";
    dmr.name = "dmr1";
    dmr.type_name = "DMR400";
    Port vout; vout.name = "v_out"; vout.side = PortSide::Output;
    dmr.outputs.push_back(vout);
    bp.add_node(std::move(dmr));

    // Wire: dmr1.v_out → bus1.v (Output → InOut = should succeed)
    Wire w1 = Wire::make("w1",
        WireEnd("dmr1", "v_out", PortSide::Output),
        WireEnd("bus1", "v", PortSide::InOut));
    EXPECT_TRUE(bp.add_wire_validated(std::move(w1)))
        << "Output to InOut should be allowed";

    // Second source node
    Node src;
    src.id = "src1";
    src.name = "src1";
    src.type_name = "Battery";
    Port src_out; src_out.name = "v_out"; src_out.side = PortSide::Output;
    src.outputs.push_back(src_out);
    bp.add_node(std::move(src));

    // Wire: src1.v_out → bus1.v (Bus allows multiple wires)
    Wire w2 = Wire::make("w2",
        WireEnd("src1", "v_out", PortSide::Output),
        WireEnd("bus1", "v", PortSide::InOut));
    EXPECT_TRUE(bp.add_wire_validated(std::move(w2)))
        << "Bus should allow multiple incoming wires";
}

// ─── Regression: Duplicate node dedup in editor format ─────────────────────

TEST(PersistTest, DuplicateNodes_DedupedOnLoad) {
    // Manually create JSON with duplicate nodes
    nlohmann::json j;
    j["devices"] = nlohmann::json::array();

    // Same node twice with different positions
    nlohmann::json dev1 = {
        {"name", "bat1"}, {"classname", "Battery"}, {"kind", "Node"},
        {"ports", {{"v_out", {{"direction", "Out"}}}}},
        {"pos", {{"x", 100.0f}, {"y", 100.0f}}},
        {"size", {{"x", 120.0f}, {"y", 80.0f}}}
    };
    j["devices"].push_back(dev1);
    // Duplicate with different position
    nlohmann::json dev2 = dev1;
    dev2["pos"] = {{"x", 500.0f}, {"y", 500.0f}};
    j["devices"].push_back(dev2);
    // Third copy
    j["devices"].push_back(dev1);

    j["wires"] = nlohmann::json::array();

    auto bp = blueprint_from_json(j.dump());
    ASSERT_TRUE(bp.has_value());

    // Should only have 1 node (duplicates removed)
    EXPECT_EQ(bp->nodes.size(), 1u) << "Duplicate nodes should be deduped on load";
    EXPECT_EQ(bp->nodes[0].id, "bat1");
    // First occurrence wins
    EXPECT_FLOAT_EQ(bp->nodes[0].pos.x, 100.0f);
}

// ─── Regression: blueprint-to-blueprint wire not dropped ───────────────────

TEST(PersistTest, BlueprintToBlueprintWire_NotDropped) {
    Blueprint bp;

    // Two Blueprint nodes (collapsed groups)
    Node bp1;
    bp1.id = "battery_bp";
    bp1.name = "battery_bp";
    bp1.type_name = "simple_battery";
    bp1.kind = NodeKind::Blueprint;
    bp1.blueprint_path = "blueprints/simple_battery.json";
    bp1.output("vout");
    bp.add_node(std::move(bp1));

    // Internal node for bp1's vout port
    Node bp1_vout;
    bp1_vout.id = "battery_bp:vout";
    bp1_vout.name = "battery_bp:vout";
    bp1_vout.type_name = "BlueprintOutput";
    bp1_vout.input("port");
    bp1_vout.output("ext");
    bp.add_node(std::move(bp1_vout));

    Node bp2;
    bp2.id = "lamp_bp";
    bp2.name = "lamp_bp";
    bp2.type_name = "lamp_pass_through";
    bp2.kind = NodeKind::Blueprint;
    bp2.blueprint_path = "blueprints/lamp_pass_through.json";
    bp2.input("vin");
    bp.add_node(std::move(bp2));

    // Internal node for bp2's vin port
    Node bp2_vin;
    bp2_vin.id = "lamp_bp:vin";
    bp2_vin.name = "lamp_bp:vin";
    bp2_vin.type_name = "BlueprintInput";
    bp2_vin.input("ext");
    bp2_vin.output("port");
    bp.add_node(std::move(bp2_vin));

    // Wire connecting two blueprint nodes
    Wire w;
    w.id = "wire_bp2bp";
    w.start = WireEnd("battery_bp", "vout", PortSide::Output);
    w.end = WireEnd("lamp_bp", "vin", PortSide::Input);
    bp.add_wire(std::move(w));

    // Convert to simulation JSON
    std::string sim_json = blueprint_to_json(bp);

    // The rewritten wire should connect battery_bp:vout.ext → lamp_bp:vin.ext
    EXPECT_NE(sim_json.find("battery_bp:vout"), std::string::npos)
        << "Wire from blueprint node should be rewritten to internal port";
    EXPECT_NE(sim_json.find("lamp_bp:vin"), std::string::npos)
        << "Wire to blueprint node should be rewritten to internal port";

    // Parse and verify connection exists
    auto parsed = blueprint_from_json(sim_json);
    ASSERT_TRUE(parsed.has_value());

    // Find the rewritten wire
    bool found_wire = false;
    for (const auto& wire : parsed->wires) {
        if (wire.start.node_id == "battery_bp:vout" && wire.end.node_id == "lamp_bp:vin") {
            found_wire = true;
            break;
        }
    }
    EXPECT_TRUE(found_wire) << "Wire between two Blueprint nodes must not be dropped";
}

// ─── Regression: VisualNode position syncs from Node data ──────────────────

TEST(PersistTest, VisualCache_PositionSync) {
    Node n;
    n.id = "test_node";
    n.name = "test_node";
    n.type_name = "Battery";
    n.kind = NodeKind::Node;
    n.at(96.0f, 192.0f);  // grid-aligned (16px grid)
    n.input("v_in");
    n.output("v_out");

    VisualNodeCache cache;
    std::vector<Wire> wires;

    // First access — creates visual at node position
    auto* visual = cache.getOrCreate(n, wires);
    ASSERT_NE(visual, nullptr);
    EXPECT_FLOAT_EQ(visual->getPosition().x, 96.0f);
    EXPECT_FLOAT_EQ(visual->getPosition().y, 192.0f);

    // Simulate external position change (e.g. load from file)
    // Use grid-aligned values (grid step = 16)
    n.pos = Pt(400.0f, 496.0f);

    // Second access — should sync position from node data
    auto* visual2 = cache.getOrCreate(n, wires);
    EXPECT_FLOAT_EQ(visual2->getPosition().x, 400.0f);
    EXPECT_FLOAT_EQ(visual2->getPosition().y, 496.0f);

    // Port position should use updated position
    Pt port_pos = visual2->getPort("v_out")->worldPosition();
    EXPECT_GT(port_pos.x, 300.0f) << "Port position should reflect updated node position";
}

// ─── Regression: Port types loaded from component registry ────────────────

TEST(PersistTest, PortTypes_LoadedFromRegistry) {
    // Create editor-format JSON WITHOUT port types (like old saves)
    nlohmann::json j;
    j["devices"] = nlohmann::json::array();

    nlohmann::json dmr = {
        {"name", "dmr1"}, {"classname", "DMR400"}, {"kind", "Node"},
        {"ports", {
            {"v_in", {{"direction", "In"}}},
            {"v_out", {{"direction", "Out"}}},
            {"v_gen_ref", {{"direction", "In"}}},
            {"lamp", {{"direction", "Out"}}}
        }},
        {"pos", {{"x", 100.0f}, {"y", 100.0f}}},
        {"size", {{"x", 120.0f}, {"y", 80.0f}}}
    };
    j["devices"].push_back(dmr);
    j["wires"] = nlohmann::json::array();

    auto bp = blueprint_from_json(j.dump());
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1u);

    // Port types should be resolved from the component registry
    const Node& loaded = bp->nodes[0];
    for (const auto& p : loaded.inputs) {
        if (p.name == "v_in" || p.name == "v_gen_ref")
            EXPECT_EQ(p.type, an24::PortType::V) << p.name << " should be type V from registry";
    }
    for (const auto& p : loaded.outputs) {
        if (p.name == "v_out" || p.name == "lamp")
            EXPECT_EQ(p.type, an24::PortType::V) << p.name << " should be type V from registry";
    }
}

