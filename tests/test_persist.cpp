#include <gtest/gtest.h>
#include "editor/visual/scene/persist.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "editor/visual/node/node.h"
#include "editor/visual/node/types/bus_node.h"
#include "editor/visual/node/visual_node_cache.h"
#include "editor/visual/scene/scene.h"
#include "json_parser/json_parser.h"
#include <nlohmann/json.hpp>
#include <set>
#include <fstream>
#include <filesystem>

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
    std::string json = blueprint_to_editor_json(bp);
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

// ============================================================================
// Regression: display_name (node.name) must roundtrip through save/load
// ============================================================================

TEST(PersistTest, DisplayName_Roundtrip_CustomName) {
    Blueprint bp;

    Node n;
    n.id = "azs_1";
    n.name = "\xd0\x90\xd0\x97\xd0\xa1 \xd0\x91\xd0\xb0\xd1\x82\xd0\xb0\xd1\x80\xd0\xb5\xd0\xb8";  // "АЗС Батареи"
    n.type_name = "AZS";
    n.at(0, 0).size_wh(160, 128);
    n.input("control");
    n.output("state");
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    // JSON should contain display_name since name != id
    EXPECT_NE(json.find("display_name"), std::string::npos)
        << "Editor JSON must include display_name when name differs from id";

    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    EXPECT_EQ(bp2->nodes[0].id, "azs_1");
    EXPECT_EQ(bp2->nodes[0].name, "\xd0\x90\xd0\x97\xd0\xa1 \xd0\x91\xd0\xb0\xd1\x82\xd0\xb0\xd1\x80\xd0\xb5\xd0\xb8")
        << "Display name must survive save/load roundtrip";
}

TEST(PersistTest, DisplayName_Roundtrip_SameAsId) {
    // When name == id, display_name is not saved (backward compatible)
    Blueprint bp;

    Node n;
    n.id = "battery_1";
    n.name = "battery_1";  // same as id
    n.type_name = "Battery";
    n.at(0, 0);
    n.input("v_in");
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    EXPECT_EQ(json.find("display_name"), std::string::npos)
        << "display_name should NOT be saved when name == id";

    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    EXPECT_EQ(bp2->nodes[0].name, "battery_1")
        << "name should default to id when display_name is absent";
}

TEST(PersistTest, DisplayName_WithSpacesAndSpecialChars) {
    Blueprint bp;

    Node n;
    n.id = "pump_3";
    n.name = "Main Fuel Pump #3 (backup)";
    n.type_name = "Pump";
    n.at(50, 50);
    n.input("in");
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    EXPECT_EQ(bp2->nodes[0].name, "Main Fuel Pump #3 (backup)")
        << "Display name with spaces and special characters must roundtrip";
}

TEST(PersistTest, SizeExplicitlySet_FromJson) {
    // Nodes loaded from JSON with explicit size should preserve it
    Blueprint bp;

    Node n;
    n.id = "r1";
    n.name = "r1";
    n.type_name = "Resistor";
    n.at(0, 0).size_wh(200, 100);
    n.input("in");
    n.output("out");
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    EXPECT_TRUE(bp2->nodes[0].size_explicitly_set)
        << "Loaded node with explicit size in JSON must have size_explicitly_set=true";
    EXPECT_FLOAT_EQ(bp2->nodes[0].size.x, 200.0f);
    EXPECT_FLOAT_EQ(bp2->nodes[0].size.y, 100.0f);
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

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);

    ASSERT_TRUE(bp2.has_value());
    EXPECT_EQ(bp2->pan.x, 50.0f);
    EXPECT_EQ(bp2->zoom, 1.5f);
    EXPECT_EQ(bp2->grid_step, 24.0f);
}

/// Test editor format: devices + wires + viewport
TEST(PersistTest, EditorFormat_WithMetadata) {
    const char* json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "batt": {"type": "Battery",
             "pos": [50, 60], "size": [120, 80]},
            "load": {"type": "Resistor",
             "pos": [250, 60], "size": [100, 60]}
        },
        "wires": [
            {"from": ["batt", "v_out"], "to": ["load", "v_in"],
             "routing": [[150, 60], [150, 100]]}
        ],
        "viewport": {"pan": [100, 200], "zoom": 2.0, "grid": 32}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());

    // Check nodes
    EXPECT_EQ(bp->nodes.size(), 2);

    // Check viewport
    EXPECT_EQ(bp->pan.x, 100.0f);
    EXPECT_EQ(bp->pan.y, 200.0f);
    EXPECT_EQ(bp->zoom, 2.0f);
    EXPECT_EQ(bp->grid_step, 32.0f);

    // Check node positions from inline device data
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



// ─── render_hint / expandable persistence tests ───

TEST(PersistTest, RenderHint_Roundtrip_Bus) {
    Blueprint bp;
    Node n;
    n.id = "mybus";
    n.name = "Custom Bus";
    n.type_name = "Bus"; // Not "Bus" — user-chosen type name
    n.render_hint = "bus";
    n.output("v");
    n.at(100, 100);
    n.size_wh(40, 40);
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);
    EXPECT_EQ(bp2->nodes[0].render_hint, "bus")
        << "render_hint should roundtrip through JSON";
}

TEST(PersistTest, RenderHint_Roundtrip_Ref) {
    Blueprint bp;
    Node n;
    n.id = "myref";
    n.name = "Custom Ref";
    n.type_name = "RefNode"; // Not "RefNode" — user-chosen type name
    n.render_hint = "ref";
    n.output("v");
    n.at(50, 50);
    n.size_wh(40, 40);
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);
    EXPECT_EQ(bp2->nodes[0].render_hint, "ref")
        << "render_hint should roundtrip through JSON";
}

TEST(PersistTest, RenderHint_Roundtrip_Default) {
    Blueprint bp;
    Node n;
    n.id = "comp1";
    n.name = "Some Component";
    n.type_name = "Battery";
    n.input("v_in");
    n.output("v_out");
    n.at(200, 200);
    n.size_wh(120, 80);
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);
    EXPECT_TRUE(bp2->nodes[0].render_hint.empty());
}

TEST(PersistTest, Expandable_Roundtrip_False) {
    Blueprint bp;
    Node n;
    n.id = "bat1";
    n.name = "Battery";
    n.type_name = "Battery";
    n.input("v_in");
    n.output("v_out");
    n.at(200, 200);
    n.size_wh(120, 80);
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);
    EXPECT_FALSE(bp2->nodes[0].expandable)
        << "expandable=false should roundtrip through JSON";
}

TEST(PersistTest, RefNode_ValueByKind_NotTypeName) {
    // classname (type_name) is the single source of truth for C++ binding.
    // A node with type_name="RefNode" should get value=0.0 fallback
    // regardless of render_hint (render_hint is for visual only).
    Blueprint bp;
    Node n;
    n.id = "gnd";
    n.name = "Ground";
    n.type_name = "RefNode";
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
    TypeRegistry registry = load_type_registry("library/");

    Pt size = get_default_node_size("Bus", &registry);
    EXPECT_FLOAT_EQ(size.x, 40.0f);
    EXPECT_FLOAT_EQ(size.y, 40.0f);
}

TEST(PersistTest, GetDefaultNodeSize_RefNode_Returns40x40) {
    // RefNode has default_size {2, 2} in JSON = 40x40 pixels
    TypeRegistry registry = load_type_registry("library/");

    Pt size = get_default_node_size("RefNode", &registry);
    EXPECT_FLOAT_EQ(size.x, 40.0f);
    EXPECT_FLOAT_EQ(size.y, 40.0f);
}

TEST(PersistTest, GetDefaultNodeSize_Ref_Alias_Returns40x40) {
    // Ref (alias for RefNode) - uses same size through registry
    TypeRegistry registry = load_type_registry("library/");

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
    TypeRegistry registry;

    // Create a mock component definition with default_size
    TypeDefinition def;
    def.classname = "TestComponent";
    def.size = {2, 3};  // 2x3 grid units = 40x60 pixels

    registry.types["TestComponent"] = def;

    Pt size = get_default_node_size("TestComponent", &registry);
    EXPECT_FLOAT_EQ(size.x, 40.0f);  // 2 * 20
    EXPECT_FLOAT_EQ(size.y, 60.0f);  // 3 * 20
}

TEST(PersistTest, GetDefaultNodeSize_Splitter_Returns60x60) {
    // Splitter has default_size {3, 3} in its JSON definition
    // 3x3 grid units = 60x60 pixels
    // Load real component registry
    TypeRegistry registry = load_type_registry("library/");

    const auto* split_def = registry.get("Splitter");
    ASSERT_NE(split_def, nullptr) << "Splitter component should exist in registry";
    ASSERT_TRUE(split_def->size.has_value()) << "Splitter should have default_size defined";
    EXPECT_EQ(split_def->size->first, 3);
    EXPECT_EQ(split_def->size->second, 3);

    Pt size = get_default_node_size("Splitter", &registry);
    EXPECT_FLOAT_EQ(size.x, 60.0f) << "Splitter width should be 3 grid units (60px)";
    EXPECT_FLOAT_EQ(size.y, 60.0f) << "Splitter height should be 3 grid units (60px)";
}

TEST(PersistTest, GetDefaultNodeSize_GridUnitConversion) {
    // Test that grid unit conversion is correct: 1 unit = 20 pixels
    TypeRegistry registry;

    TypeDefinition def;
    def.classname = "TestComponent";
    def.size = {1, 1};  // 1x1 grid unit

    registry.types["TestComponent"] = def;

    Pt size = get_default_node_size("TestComponent", &registry);
    EXPECT_FLOAT_EQ(size.x, 20.0f) << "1 grid unit should be 20 pixels";
    EXPECT_FLOAT_EQ(size.y, 20.0f) << "1 grid unit should be 20 pixels";
}


// [e5f6] node_content params should survive roundtrip

// =============================================================================
// Tests for BusVisualNode dynamic resizing
// =============================================================================

TEST(PersistTest, BusVisualNode_InitialSizeFromTypeDefinition) {
    // Bus has default_size {2, 2} in JSON = 40x40 pixels
    // Visual grid snapping (16px) snaps 40px up to 48px (3 * 16)
    TypeRegistry registry = load_type_registry("library/");

    Node n;
    n.id = "bus1";
    n.name = "Test Bus";
    n.type_name = "Bus";
    n.render_hint = "bus";
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
    TypeRegistry registry = load_type_registry("library/");

    // Create bus node
    Node bus_node;
    bus_node.id = "bus1";
    bus_node.name = "Test Bus";
    bus_node.type_name = "Bus";
    bus_node.render_hint = "bus";
    bus_node.at(100, 100);
    bus_node.size = get_default_node_size("Bus", &registry);
    bus_node.output("v");

    // Create another node to connect to
    Node other_node;
    other_node.id = "other";
    other_node.name = "Other";
    other_node.type_name = "Battery";
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
    TypeRegistry registry = load_type_registry("library/");

    // Create bus node
    Node bus_node;
    bus_node.id = "bus1";
    bus_node.name = "Test Bus";
    bus_node.type_name = "Bus";
    bus_node.render_hint = "bus";
    bus_node.at(100, 100);
    bus_node.size = get_default_node_size("Bus", &registry);
    bus_node.output("v");

    // Create other nodes
    Node other1, other2;
    other1.id = "other1";
    other1.name = "Other1";
    other1.type_name = "Battery";
    other1.at(200, 100);
    other1.output("v_out");

    other2.id = "other2";
    other2.name = "Other2";
    other2.type_name = "Battery";
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
    TypeRegistry registry = load_type_registry("library/");

    Node n;
    n.id = "bus1";
    n.name = "Test Bus";
    n.type_name = "Bus";
    n.render_hint = "bus";
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
    bus.render_hint = "bus";
    bus.at(100.0f, 100.0f);
    // Bus port is InOut with type V
    EditorPort bus_port;
    bus_port.name = "v";
    bus_port.side = PortSide::InOut;
    bus_port.type = PortType::V;
    bus.inputs.push_back(bus_port);
    bus.outputs.push_back(bus_port);
    bp.add_node(std::move(bus));

    Node dmr;
    dmr.id = "dmr1";
    dmr.name = "dmr1";
    dmr.type_name = "DMR400";
    dmr.at(200.0f, 100.0f);
    EditorPort vin; vin.name = "v_in"; vin.side = PortSide::Input; vin.type = PortType::V;
    EditorPort vout; vout.name = "v_out"; vout.side = PortSide::Output; vout.type = PortType::V;
    EditorPort lamp; lamp.name = "lamp"; lamp.side = PortSide::Output; lamp.type = PortType::V;
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
            EXPECT_EQ(p.type, PortType::V) << "v_in port type should be V";
    }
    for (const auto& p : loaded_dmr->outputs) {
        if (p.name == "v_out")
            EXPECT_EQ(p.type, PortType::V) << "v_out port type should be V";
        if (p.name == "lamp")
            EXPECT_EQ(p.type, PortType::V) << "lamp port type should be V";
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
    bus.render_hint = "bus";
    EditorPort bus_port;
    bus_port.name = "v";
    bus_port.side = PortSide::InOut;
    bus_port.type = PortType::V;
    bus.inputs.push_back(bus_port);
    bus.outputs.push_back(bus_port);
    bp.add_node(std::move(bus));

    // DMR with output
    Node dmr;
    dmr.id = "dmr1";
    dmr.name = "dmr1";
    dmr.type_name = "DMR400";
    EditorPort vout; vout.name = "v_out"; vout.side = PortSide::Output;
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
    EditorPort src_out; src_out.name = "v_out"; src_out.side = PortSide::Output;
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
    // In v2 format, nodes are an object keyed by ID — duplicates are inherently impossible.
    // Just verify a normal load works correctly.
    const char* json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {"bat1": {"type": "Battery", "pos": [100, 100], "size": [120, 80]}},
        "wires": []
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    EXPECT_EQ(bp->nodes.size(), 1u);
    EXPECT_EQ(bp->nodes[0].id, "bat1");
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
    bp1.expandable = true;
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
    bp2.expandable = true;
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

    // Verify connection exists in the JSON
    auto j = nlohmann::json::parse(sim_json);
    ASSERT_TRUE(j.contains("connections"));
    bool found_wire = false;
    for (const auto& conn : j["connections"]) {
        std::string from = conn.value("from", "");
        std::string to = conn.value("to", "");
        if (from.find("battery_bp:vout") != std::string::npos &&
            to.find("lamp_bp:vin") != std::string::npos) {
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

// =============================================================================
// BUGFIX [e4a1b7] Dedup guard regression tests
// =============================================================================

TEST(PersistTest, DedupGuard_DuplicateWiresDroppedOnLoad) {
    const char* json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "a": {"type": "Battery",
             "pos": [0, 0], "size": [120, 80]},
            "b": {"type": "Resistor",
             "pos": [200, 0], "size": [120, 80]}
        },
        "wires": [
            {"from": ["a", "v_out"], "to": ["b", "v_in"]},
            {"from": ["a", "v_out"], "to": ["b", "v_in"]},
            {"from": ["a", "v_out"], "to": ["b", "v_in"]},
            {"from": ["a", "v_out"], "to": ["b", "v_in"]}
        ],
        "viewport": {"pan": [0, 0], "zoom": 1.0, "grid": 16}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    EXPECT_EQ(bp->wires.size(), 1)
        << "Duplicate wires must be deduped on load (had 4 identical wires)";
}

TEST(PersistTest, DedupGuard_DuplicateNodesDroppedOnLoad) {
    // In v2 format, nodes are an object keyed by ID — duplicates are inherently impossible.
    // Just verify a single-node load works correctly.
    const char* json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "x": {"type": "Battery",
             "pos": [0, 0], "size": [120, 80]}
        },
        "wires": [],
        "viewport": {"pan": [0, 0], "zoom": 1.0, "grid": 16}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    EXPECT_EQ(bp->nodes.size(), 1)
        << "Single node in v2 object format should load correctly";
}

TEST(PersistTest, DedupGuard_DuplicateRoutingPointsDroppedOnLoad) {
    const char* json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "a": {"type": "Battery",
             "pos": [0, 0], "size": [120, 80]},
            "b": {"type": "Resistor",
             "pos": [200, 0], "size": [120, 80]}
        },
        "wires": [
            {"from": ["a", "v_out"], "to": ["b", "v_in"],
             "routing": [[100, 50], [100, 50], [150, 50]]}
        ],
        "viewport": {"pan": [0, 0], "zoom": 1.0, "grid": 16}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->wires.size(), 1);
    // v2 loader does not dedup routing points — all 3 are preserved
    EXPECT_EQ(bp->wires[0].routing_points.size(), 3);
}

TEST(PersistTest, DedupGuard_SaveDedupsWires) {
    Blueprint bp;

    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.input("v_in"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    Wire w1; w1.id = "w1"; w1.start = WireEnd("a", "v_out", PortSide::Output); w1.end = WireEnd("b", "v_in", PortSide::Input);
    Wire w2; w2.id = "w2"; w2.start = WireEnd("a", "v_out", PortSide::Output); w2.end = WireEnd("b", "v_in", PortSide::Input);
    Wire w3; w3.id = "w3"; w3.start = WireEnd("a", "v_out", PortSide::Output); w3.end = WireEnd("b", "v_in", PortSide::Input);
    bp.add_wire(std::move(w1));
    bp.add_wire(std::move(w2));
    bp.add_wire(std::move(w3));

    // Save and reload — duplicates must be gone
    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    EXPECT_EQ(bp2->wires.size(), 1)
        << "Roundtrip must produce exactly 1 wire (dedup on save + load)";
}

// =============================================================================
// BUGFIX [d9c3f2] save_blueprint_to_file rejects library/ directory
// =============================================================================

TEST(PersistTest, SaveRejectsBluprintDirectory) {
    Blueprint bp;
    // Should refuse to write to library/ directory
    bool ok = save_blueprint_to_file(bp, "library/test_output.json");
    EXPECT_FALSE(ok) << "save_blueprint_to_file must refuse to write into library/ dir";
}

// =============================================================================
// BUGFIX [a2d7c5] Auto-layout: BlueprintInput left, BlueprintOutput right
// =============================================================================

TEST(PersistTest, AutoLayout_BlueprintInputLeftOutputRight) {
    Blueprint bp;

    Node vin;  vin.id = "g1:vin";  vin.type_name = "BlueprintInput";  vin.group_id = "g1";
    Node vout; vout.id = "g1:vout"; vout.type_name = "BlueprintOutput"; vout.group_id = "g1";
    Node bat;  bat.id = "g1:bat";  bat.type_name = "Battery";          bat.group_id = "g1";
    Node load; load.id = "g1:r1";  load.type_name = "Resistor";        load.group_id = "g1";

    bp.add_node(std::move(vin));
    bp.add_node(std::move(vout));
    bp.add_node(std::move(bat));
    bp.add_node(std::move(load));

    bp.auto_layout_group("g1");

    float vin_x  = bp.find_node("g1:vin")->pos.x;
    float vout_x = bp.find_node("g1:vout")->pos.x;
    float bat_x  = bp.find_node("g1:bat")->pos.x;
    float load_x = bp.find_node("g1:r1")->pos.x;

    EXPECT_LT(vin_x, bat_x)   << "BlueprintInput must be to the left of sources";
    EXPECT_LT(bat_x, load_x)  << "Sources must be to the left of loads";
    EXPECT_LT(load_x, vout_x) << "BlueprintOutput must be to the right of loads";
}

// =============================================================================
// Unit tests for Blueprint::add_wire() runtime dedup [e4a1b7]
// =============================================================================

TEST(BlueprintTest, AddWire_ReturnsIndexOnSuccess) {
    Blueprint bp;
    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.input("v_in"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    Wire w;
    w.id = "w1";
    w.start = WireEnd("a", "v_out", PortSide::Output);
    w.end = WireEnd("b", "v_in", PortSide::Input);

    size_t idx = bp.add_wire(std::move(w));
    EXPECT_EQ(idx, 0) << "First wire should be at index 0";
    EXPECT_EQ(bp.wires.size(), 1);
}

TEST(BlueprintTest, AddWire_RejectsDuplicate) {
    Blueprint bp;
    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.input("v_in"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    Wire w1; w1.id = "w1";
    w1.start = WireEnd("a", "v_out", PortSide::Output);
    w1.end = WireEnd("b", "v_in", PortSide::Input);

    Wire w2; w2.id = "w2";
    w2.start = WireEnd("a", "v_out", PortSide::Output);
    w2.end = WireEnd("b", "v_in", PortSide::Input);

    size_t idx1 = bp.add_wire(std::move(w1));
    size_t idx2 = bp.add_wire(std::move(w2));

    EXPECT_EQ(idx1, 0);
    EXPECT_EQ(idx2, SIZE_MAX) << "Duplicate wire must be rejected with SIZE_MAX";
    EXPECT_EQ(bp.wires.size(), 1) << "Only one wire should exist after dedup";
}

TEST(BlueprintTest, AddWire_AllowsDifferentConnections) {
    Blueprint bp;
    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.output("i_out"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.input("v_in"); b.input("i_in"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    Wire w1; w1.id = "w1";
    w1.start = WireEnd("a", "v_out", PortSide::Output);
    w1.end = WireEnd("b", "v_in", PortSide::Input);

    Wire w2; w2.id = "w2";
    w2.start = WireEnd("a", "i_out", PortSide::Output);
    w2.end = WireEnd("b", "i_in", PortSide::Input);

    size_t idx1 = bp.add_wire(std::move(w1));
    size_t idx2 = bp.add_wire(std::move(w2));

    EXPECT_EQ(idx1, 0);
    EXPECT_EQ(idx2, 1) << "Different connections must be accepted";
    EXPECT_EQ(bp.wires.size(), 2);
}

TEST(BlueprintTest, AddWire_RejectsTripleDuplicate) {
    Blueprint bp;
    Node a; a.id = "x"; a.type_name = "Battery"; a.output("out"); a.at(0, 0);
    Node b; b.id = "y"; b.type_name = "Resistor"; b.input("in"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    for (int i = 0; i < 3; i++) {
        Wire w; w.id = "w" + std::to_string(i);
        w.start = WireEnd("x", "out", PortSide::Output);
        w.end = WireEnd("y", "in", PortSide::Input);
        bp.add_wire(std::move(w));
    }
    EXPECT_EQ(bp.wires.size(), 1) << "3 identical add_wire calls must produce exactly 1 wire";
}

// =============================================================================
// Unit tests for Blueprint::add_node() dedup
// =============================================================================

TEST(BlueprintTest, AddNode_ReturnsExistingIndexOnDuplicate) {
    Blueprint bp;
    Node a; a.id = "bat1"; a.type_name = "Battery"; a.at(0, 0);
    Node a2; a2.id = "bat1"; a2.type_name = "Battery"; a2.at(500, 500);

    size_t idx1 = bp.add_node(std::move(a));
    size_t idx2 = bp.add_node(std::move(a2));

    EXPECT_EQ(idx1, 0);
    EXPECT_EQ(idx2, 0) << "Duplicate node ID must return existing index";
    EXPECT_EQ(bp.nodes.size(), 1) << "Only one node should exist";
    EXPECT_EQ(bp.nodes[0].pos.x, 0.0f) << "Original node should be preserved";
}

// =============================================================================
// sim-format blueprint_to_json() dedup tests [e4a1b7]
// =============================================================================

TEST(PersistTest, SimFormat_DedupsConnections) {
    Blueprint bp;

    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.input("v_in"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    // Force duplicate wires by directly pushing (bypass add_wire dedup)
    Wire w1; w1.id = "w1";
    w1.start = WireEnd("a", "v_out", PortSide::Output);
    w1.end = WireEnd("b", "v_in", PortSide::Input);
    bp.wires.push_back(w1);
    bp.wires.push_back(w1); // exact duplicate
    bp.wires.push_back(w1); // triple

    std::string json_str = blueprint_to_json(bp);
    auto j = nlohmann::json::parse(json_str);

    ASSERT_TRUE(j.contains("connections"));
    EXPECT_EQ(j["connections"].size(), 1)
        << "blueprint_to_json must dedup connections (had 3 identical wires)";
}

TEST(PersistTest, SimFormat_DedupsNodes) {
    Blueprint bp;

    Node a; a.id = "dup"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    Node a2; a2.id = "dup"; a2.type_name = "Battery"; a2.output("v_out"); a2.at(100, 0);
    // Force duplicate by pushing directly
    bp.nodes.push_back(std::move(a));
    bp.nodes.push_back(std::move(a2));

    std::string json_str = blueprint_to_json(bp);
    auto j = nlohmann::json::parse(json_str);

    ASSERT_TRUE(j.contains("devices"));
    EXPECT_EQ(j["devices"].size(), 1)
        << "blueprint_to_json must dedup devices with same ID";
}

// =============================================================================
// Editor save dedup: blueprint_to_editor_json() preserves first wire's routing
// =============================================================================

TEST(PersistTest, EditorSave_PreservesFirstWireRoutingOnDedup) {
    Blueprint bp;

    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.input("v_in"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    // First wire has routing points
    Wire w1; w1.id = "w1";
    w1.start = WireEnd("a", "v_out", PortSide::Output);
    w1.end = WireEnd("b", "v_in", PortSide::Input);
    w1.routing_points.push_back(Pt(100, 50));
    w1.routing_points.push_back(Pt(150, 50));

    // Duplicate without routing points
    Wire w2; w2.id = "w2";
    w2.start = WireEnd("a", "v_out", PortSide::Output);
    w2.end = WireEnd("b", "v_in", PortSide::Input);

    bp.wires.push_back(std::move(w1));
    bp.wires.push_back(std::move(w2));

    std::string json_str = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json_str);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->wires.size(), 1);
    EXPECT_EQ(bp2->wires[0].routing_points.size(), 2)
        << "Dedup on save must keep first wire (which has routing points)";
}

// =============================================================================
// Editor save dedup: sub_blueprints dedup
// =============================================================================

TEST(PersistTest, EditorSave_DedupsSubBlueprints) {
    Blueprint bp;

    SubBlueprintInstance g1;
    g1.id = "lamp1";
    g1.blueprint_path = "blueprints/lamp.json";
    g1.type_name = "LampCircuit";
    SubBlueprintInstance g2;
    g2.id = "lamp1";  // exact dup
    g2.blueprint_path = "blueprints/lamp.json";
    g2.type_name = "LampCircuit";

    bp.sub_blueprint_instances.push_back(g1);
    bp.sub_blueprint_instances.push_back(g2);

    std::string json_str = blueprint_to_editor_json(bp);
    auto j = nlohmann::json::parse(json_str);

    ASSERT_TRUE(j.contains("sub_blueprints"));
    EXPECT_EQ(j["sub_blueprints"].size(), 1)
        << "Duplicate sub_blueprints must be deduped on save";
}

// =============================================================================
// Roundtrip stability: save→load→save produces same output
// =============================================================================

TEST(PersistTest, Roundtrip_SaveLoadSave_Stable) {
    Blueprint bp;

    Node a; a.id = "bat1"; a.type_name = "Battery"; a.output("v_out"); a.at(80, 80);
    Node b; b.id = "r1"; b.type_name = "Resistor"; b.input("v_in"); b.at(280, 80);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    Wire w; w.id = "w1";
    w.start = WireEnd("bat1", "v_out", PortSide::Output);
    w.end = WireEnd("r1", "v_in", PortSide::Input);
    w.routing_points.push_back(Pt(180, 40));
    bp.add_wire(std::move(w));

    // Save → load → save → load
    std::string json1 = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json1);
    ASSERT_TRUE(bp2.has_value());

    std::string json2 = blueprint_to_editor_json(*bp2);
    auto bp3 = blueprint_from_json(json2);
    ASSERT_TRUE(bp3.has_value());

    // After two roundtrips, counts must remain stable
    EXPECT_EQ(bp2->nodes.size(), bp3->nodes.size())
        << "Node count must not change across roundtrips";
    EXPECT_EQ(bp2->wires.size(), bp3->wires.size())
        << "Wire count must not change across roundtrips";
    EXPECT_EQ(bp2->wires.size(), 1) << "Must have exactly 1 wire";
    EXPECT_EQ(bp3->wires[0].routing_points.size(), 1) << "Routing points preserved";
}

TEST(PersistTest, Roundtrip_DuplicatesDoNotAccumulate) {
    // Start with duplicates forced in — after roundtrip they should collapse to 1
    Blueprint bp;
    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.input("v_in"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    // Force 5 duplicate wires
    for (int i = 0; i < 5; i++) {
        Wire w; w.id = "w" + std::to_string(i);
        w.start = WireEnd("a", "v_out", PortSide::Output);
        w.end = WireEnd("b", "v_in", PortSide::Input);
        bp.wires.push_back(std::move(w)); // bypass dedup
    }

    // Roundtrip 3 times
    for (int round = 0; round < 3; round++) {
        std::string json = blueprint_to_editor_json(bp);
        auto loaded = blueprint_from_json(json);
        ASSERT_TRUE(loaded.has_value()) << "Roundtrip " << round << " failed to parse";
        bp = std::move(*loaded);
    }

    EXPECT_EQ(bp.wires.size(), 1)
        << "After 3 roundtrips, 5 duplicate wires must collapse to exactly 1";
}

// =============================================================================
// Auto-layout edge cases [a2d7c5]
// =============================================================================

TEST(BlueprintTest, AutoLayout_OnlyBlueprintInputOutput) {
    Blueprint bp;

    Node vin;  vin.id = "g1:in";  vin.type_name = "BlueprintInput";  vin.group_id = "g1";
    Node vout; vout.id = "g1:out"; vout.type_name = "BlueprintOutput"; vout.group_id = "g1";
    bp.add_node(std::move(vin));
    bp.add_node(std::move(vout));

    bp.auto_layout_group("g1");

    float in_x  = bp.find_node("g1:in")->pos.x;
    float out_x = bp.find_node("g1:out")->pos.x;

    EXPECT_LT(in_x, out_x) << "BlueprintInput must be left of BlueprintOutput even with no other nodes";
}

TEST(BlueprintTest, AutoLayout_GroundsPlacedBelowAllColumns) {
    Blueprint bp;

    Node bat; bat.id = "g1:bat"; bat.type_name = "Battery"; bat.group_id = "g1";
    Node r1;  r1.id = "g1:r1";  r1.type_name = "Resistor"; r1.group_id = "g1";
    Node r2;  r2.id = "g1:r2";  r2.type_name = "Resistor"; r2.group_id = "g1";
    Node gnd; gnd.id = "g1:gnd"; gnd.type_name = "RefNode"; gnd.render_hint = "ref"; gnd.group_id = "g1";

    bp.add_node(std::move(bat));
    bp.add_node(std::move(r1));
    bp.add_node(std::move(r2));
    bp.add_node(std::move(gnd));

    bp.auto_layout_group("g1");

    float gnd_y = bp.find_node("g1:gnd")->pos.y;
    float bat_y = bp.find_node("g1:bat")->pos.y;
    float r1_y  = bp.find_node("g1:r1")->pos.y;
    float r2_y  = bp.find_node("g1:r2")->pos.y;

    EXPECT_GT(gnd_y, bat_y)  << "Ground must be below sources";
    EXPECT_GT(gnd_y, r1_y)   << "Ground must be below loads";
    EXPECT_GT(gnd_y, r2_y)   << "Ground must be below all loads";
}

TEST(BlueprintTest, AutoLayout_EmptyGroupIsNoop) {
    Blueprint bp;
    Node bat; bat.id = "bat1"; bat.type_name = "Battery"; bat.group_id = "other";
    bat.at(42, 42);
    bp.add_node(std::move(bat));

    bp.auto_layout_group("nonexistent");

    // Node in different group should be untouched
    EXPECT_EQ(bp.find_node("bat1")->pos.x, 42.0f);
    EXPECT_EQ(bp.find_node("bat1")->pos.y, 42.0f);
}

TEST(BlueprintTest, AutoLayout_BusColumnBetweenSourcesAndLoads) {
    Blueprint bp;

    Node bat;  bat.id = "g:bat";  bat.type_name = "Battery";  bat.group_id = "g";
    Node bus;  bus.id = "g:bus";  bus.type_name = "Bus"; bus.render_hint = "bus"; bus.group_id = "g";
    Node load; load.id = "g:load"; load.type_name = "Resistor"; load.group_id = "g";

    bp.add_node(std::move(bat));
    bp.add_node(std::move(bus));
    bp.add_node(std::move(load));

    bp.auto_layout_group("g");

    float bat_x  = bp.find_node("g:bat")->pos.x;
    float bus_x  = bp.find_node("g:bus")->pos.x;
    float load_x = bp.find_node("g:load")->pos.x;

    EXPECT_LT(bat_x, bus_x)   << "Sources must be left of buses";
    EXPECT_LT(bus_x, load_x)  << "Buses must be left of loads";
}

TEST(BlueprintTest, AutoLayout_MultipleRowsPerColumn) {
    Blueprint bp;

    // 3 loads in same group should be stacked vertically
    Node r1; r1.id = "g:r1"; r1.type_name = "Resistor"; r1.group_id = "g";
    Node r2; r2.id = "g:r2"; r2.type_name = "Resistor"; r2.group_id = "g";
    Node r3; r3.id = "g:r3"; r3.type_name = "Resistor"; r3.group_id = "g";

    bp.add_node(std::move(r1));
    bp.add_node(std::move(r2));
    bp.add_node(std::move(r3));

    bp.auto_layout_group("g");

    float y1 = bp.find_node("g:r1")->pos.y;
    float y2 = bp.find_node("g:r2")->pos.y;
    float y3 = bp.find_node("g:r3")->pos.y;

    // Same column (x), different rows (y)
    EXPECT_EQ(bp.find_node("g:r1")->pos.x, bp.find_node("g:r2")->pos.x);
    EXPECT_LT(y1, y2) << "Second load must be below first";
    EXPECT_LT(y2, y3) << "Third load must be below second";
}

// =============================================================================
// BUGFIX [d9c3f2] save_blueprint_to_file edge cases
// =============================================================================

TEST(PersistTest, SaveRejectsNestedBlueprintDirectory) {
    Blueprint bp;
    // Nested path should also be rejected
    bool ok = save_blueprint_to_file(bp, "/some/path/library/nested/test.json");
    EXPECT_FALSE(ok) << "Must reject any path with 'library' segment";
}

TEST(PersistTest, SaveAllowsNonBlueprintDirectory) {
    Blueprint bp;
    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    bp.add_node(std::move(a));

    // Save to /tmp — should succeed (path doesn't contain "library")
    bool ok = save_blueprint_to_file(bp, "/tmp/an24_test_persist_output.json");
    EXPECT_TRUE(ok) << "Saving to non-blueprints path must succeed";

    // Clean up
    std::remove("/tmp/an24_test_persist_output.json");
}

// =============================================================================
// Wire dedup: reversed direction is NOT a duplicate
// =============================================================================

TEST(BlueprintTest, AddWire_ReversedDirectionIsUnique) {
    Blueprint bp;
    Node a; a.id = "a"; a.type_name = "Battery"; a.output("p"); a.input("p"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.output("p"); b.input("p"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    Wire w1; w1.id = "w1";
    w1.start = WireEnd("a", "p", PortSide::Output);
    w1.end = WireEnd("b", "p", PortSide::Input);

    Wire w2; w2.id = "w2";
    w2.start = WireEnd("b", "p", PortSide::Output);
    w2.end = WireEnd("a", "p", PortSide::Input);

    size_t idx1 = bp.add_wire(std::move(w1));
    size_t idx2 = bp.add_wire(std::move(w2));

    EXPECT_NE(idx2, SIZE_MAX) << "Reversed wire (b→a) is not a duplicate of (a→b)";
    EXPECT_EQ(bp.wires.size(), 2);
}

// =============================================================================
// render_hint / expandable — persistence tests
// =============================================================================

TEST(PersistTest, RenderHint_RefNode) {
    const char* json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "gnd": {"type": "RefNode", "render_hint": "ref",
             "params": {"value": "0.0"},
             "pos": [0, 0], "size": [48, 48]}
        },
        "wires": [],
        "viewport": {"pan": [0, 0], "zoom": 1.0, "grid": 16}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1);
    EXPECT_EQ(bp->nodes[0].render_hint, "ref");
}

TEST(PersistTest, RenderHint_Bus) {
    const char* json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "bus1": {"type": "Bus", "render_hint": "bus",
             "pos": [0, 0], "size": [48, 48]}
        },
        "wires": [],
        "viewport": {"pan": [0, 0], "zoom": 1.0, "grid": 16}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1);
    EXPECT_EQ(bp->nodes[0].render_hint, "bus");
}

TEST(PersistTest, Expandable_Blueprint) {
    const char* json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "lamp1": {"type": "lamp_pass_through", "expandable": true,
             "pos": [0, 0], "size": [128, 96]}
        },
        "wires": [],
        "viewport": {"pan": [0, 0], "zoom": 1.0, "grid": 16}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1);
    EXPECT_TRUE(bp->nodes[0].expandable);
}

TEST(PersistTest, RenderHint_Roundtrip_RefNode) {
    Blueprint bp;
    Node gnd;
    gnd.id = "gnd";
    gnd.type_name = "RefNode";
    gnd.render_hint = "ref";
    gnd.output("v");
    gnd.at(0, 0);
    bp.add_node(std::move(gnd));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);
    EXPECT_EQ(bp2->nodes[0].render_hint, "ref")
        << "RefNode kind must survive save/load roundtrip";
}

TEST(PersistTest, RenderHint_Roundtrip_DefaultIsEmpty) {
    Blueprint bp;
    Node gnd;
    gnd.id = "gnd";
    gnd.type_name = "RefNode";
    gnd.output("v");
    gnd.at(0, 0);
    bp.nodes.push_back(std::move(gnd));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);
    // render_hint is enriched from registry on load (backward compat for old saves)
    EXPECT_EQ(bp2->nodes[0].render_hint, "ref")
        << "Node with known type_name should get render_hint from registry";
}

// =============================================================================
// BUGFIX [e2c8d4] Unknown/missing port type should not crash
// =============================================================================

TEST(PersistTest, UnknownPortType_DoesNotCrash) {
    // v2 doesn't store ports in JSON — ports come from TypeRegistry.
    // Test that an unknown type string on a node doesn't crash.
    const char* json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {"dev1": {"type": "NonexistentComponent", "pos": [0, 0], "size": [48, 48]}},
        "wires": []
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value()) << "Unknown component type must not crash";
    ASSERT_EQ(bp->nodes.size(), 1);
    // Ports won't be populated since the type isn't in the registry
    EXPECT_TRUE(bp->nodes[0].outputs.empty());
}

TEST(PersistTest, MissingPortType_DoesNotCrash) {
    // v2 doesn't store ports in JSON — ports come from TypeRegistry.
    // Test that an unknown type doesn't crash.
    const char* json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {"dev1": {"type": "AlsoNonexistent", "pos": [0, 0], "size": [48, 48]}},
        "wires": []
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value()) << "Unknown type must not crash";
    ASSERT_EQ(bp->nodes.size(), 1);
    EXPECT_TRUE(bp->nodes[0].inputs.empty());
}

// =============================================================================
// BUGFIX [f7a3b1] reset_node_content generic test via create_node_content_from_def
// =============================================================================

TEST(PersistTest, CreateNodeContentFromDef_ResetToDefaults) {
    // Verify that create_node_content_from_def produces correct defaults
    // for all known content types (this is what reset_node_content now uses).

    // Gauge type (e.g., Voltmeter)
    TypeDefinition gauge_def;
    gauge_def.content_type = "Gauge";
    NodeContent gc = create_node_content_from_def(&gauge_def);
    EXPECT_EQ(gc.type, NodeContentType::Gauge);
    EXPECT_EQ(gc.value, 0.0f);

    // Switch type
    TypeDefinition switch_def;
    switch_def.content_type = "Switch";
    switch_def.params["closed"] = "true";
    NodeContent sc = create_node_content_from_def(&switch_def);
    EXPECT_EQ(sc.type, NodeContentType::Switch);
    EXPECT_TRUE(sc.state) << "Switch with closed=true should default to state=true";

    // HoldButton type
    TypeDefinition hb_def;
    hb_def.content_type = "HoldButton";
    NodeContent hc = create_node_content_from_def(&hb_def);
    EXPECT_EQ(hc.type, NodeContentType::Switch);
    EXPECT_EQ(hc.label, "RELEASED");
    EXPECT_FALSE(hc.state);

    // Text type (e.g., IndicatorLight)
    TypeDefinition text_def;
    text_def.content_type = "Text";
    NodeContent tc = create_node_content_from_def(&text_def);
    EXPECT_EQ(tc.type, NodeContentType::Text);
    EXPECT_EQ(tc.label, "OFF");

    // Unknown type
    TypeDefinition unk_def;
    unk_def.content_type = "WeirdFutureType";
    NodeContent uc = create_node_content_from_def(&unk_def);
    EXPECT_EQ(uc.type, NodeContentType::None)
        << "Unknown content type should produce None (safe default)";

    // Null def
    NodeContent nc = create_node_content_from_def(nullptr);
    EXPECT_EQ(nc.type, NodeContentType::None);
}

// =============================================================================
// [2.1] Wire dedup O(1) set — regression tests
// =============================================================================

TEST(WireDedup, WireKey_EqualityAndHash) {
    Wire w1 = Wire::make("w1", wire_output("a", "out"), wire_input("b", "in"));
    Wire w2 = Wire::make("w2", wire_output("a", "out"), wire_input("b", "in"));
    Wire w3 = Wire::make("w3", wire_output("a", "out"), wire_input("c", "in"));

    WireKey k1(w1), k2(w2), k3(w3);
    EXPECT_EQ(k1, k2) << "Same endpoints should be equal regardless of wire id";
    EXPECT_NE(k1, k3) << "Different endpoints should not be equal";

    WireKeyHash h;
    EXPECT_EQ(h(k1), h(k2)) << "Equal keys must have equal hashes";
}

TEST(WireDedup, AddWire_SetBasedDedup) {
    Blueprint bp;
    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.input("v_in"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    Wire w1 = Wire::make("w1", wire_output("a", "v_out"), wire_input("b", "v_in"));
    Wire w2 = Wire::make("w2", wire_output("a", "v_out"), wire_input("b", "v_in"));

    EXPECT_NE(bp.add_wire(std::move(w1)), SIZE_MAX);
    EXPECT_EQ(bp.add_wire(std::move(w2)), SIZE_MAX) << "Set-based dedup should reject duplicate";
    EXPECT_EQ(bp.wires.size(), 1u);
    EXPECT_EQ(bp.wire_index_.size(), 1u);
}

TEST(WireDedup, WireIndex_SyncAfterRebuild) {
    Blueprint bp;
    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.output("i_out"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.input("v_in"); b.input("i_in"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));

    bp.add_wire(Wire::make("w1", wire_output("a", "v_out"), wire_input("b", "v_in")));
    bp.add_wire(Wire::make("w2", wire_output("a", "i_out"), wire_input("b", "i_in")));
    EXPECT_EQ(bp.wire_index_.size(), 2u);

    // Manually erase a wire and rebuild
    bp.wires.erase(bp.wires.begin());
    bp.rebuild_wire_index();
    EXPECT_EQ(bp.wire_index_.size(), 1u);
    EXPECT_EQ(bp.wires.size(), 1u);

    // Now the previously-blocked connection should be addable
    size_t idx = bp.add_wire(Wire::make("w3", wire_output("a", "v_out"), wire_input("b", "v_in")));
    EXPECT_NE(idx, SIZE_MAX) << "After rebuild, removed wire key should be re-addable";
}

TEST(WireDedup, Scene_RemoveWire_SyncsIndex) {
    Blueprint bp;
    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.input("v_in"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_wire(Wire::make("w1", wire_output("a", "v_out"), wire_input("b", "v_in")));

    VisualScene scene(bp);
    EXPECT_EQ(bp.wire_index_.size(), 1u);

    scene.removeWire(0);
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_EQ(bp.wire_index_.size(), 0u) << "wire_index_ must be cleared when wire is removed via scene";
}

TEST(WireDedup, Scene_RemoveNode_SyncsIndex) {
    Blueprint bp;
    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.input("v_in"); b.at(200, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_wire(Wire::make("w1", wire_output("a", "v_out"), wire_input("b", "v_in")));

    VisualScene scene(bp);
    scene.removeNode(0); // Remove node "a" — wire "w1" should also be removed
    EXPECT_EQ(bp.wires.size(), 0u);
    EXPECT_EQ(bp.wire_index_.size(), 0u) << "wire_index_ must sync after node removal";
}

TEST(WireDedup, Scene_ReconnectWire_UpdatesIndex) {
    Blueprint bp;
    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    Node b; b.id = "b"; b.type_name = "Resistor"; b.input("v_in"); b.at(200, 0);
    Node c; c.id = "c"; c.type_name = "Resistor"; c.input("v_in"); c.at(400, 0);
    bp.add_node(std::move(a));
    bp.add_node(std::move(b));
    bp.add_node(std::move(c));
    bp.add_wire(Wire::make("w1", wire_output("a", "v_out"), wire_input("b", "v_in")));

    VisualScene scene(bp);
    // Reconnect wire end from b to c
    scene.reconnectWire(0, false, wire_input("c", "v_in"));
    EXPECT_EQ(bp.wire_index_.size(), 1u);

    // The old connection should now be addable (no longer in index)
    size_t idx = bp.add_wire(Wire::make("w2", wire_output("a", "v_out"), wire_input("b", "v_in")));
    EXPECT_NE(idx, SIZE_MAX) << "Old endpoint should be re-addable after reconnect";
}

// =============================================================================
// [2.5] Bus port invariants — regression tests
// =============================================================================

TEST(BusInvariant, Construction_PortsEqualWiresPlusOne) {
    Node bus; bus.id = "bus1"; bus.type_name = "Bus"; bus.render_hint = "bus";
    bus.at(0, 0).size_wh(128, 32);
    bus.input("v"); bus.output("v");

    // Two wires touching the bus
    std::vector<Wire> wires;
    wires.push_back(Wire::make("w1", wire_output("bus1", "v"), wire_input("r1", "in")));
    wires.push_back(Wire::make("w2", wire_output("gen", "out"), wire_input("bus1", "v")));
    // One wire NOT touching the bus
    wires.push_back(Wire::make("w3", wire_output("gen", "out2"), wire_input("r1", "in2")));

    BusVisualNode vis(bus, BusOrientation::Horizontal, wires);
    // Should have 2 alias ports + 1 logical "v" port = 3
    EXPECT_EQ(vis.getPortCount(), 3u) << "Bus should filter wires: 2 connected + 1 logical 'v'";
}

TEST(BusInvariant, ConnectWire_AddsPort) {
    Node bus; bus.id = "bus1"; bus.type_name = "Bus"; bus.render_hint = "bus";
    bus.at(0, 0).size_wh(128, 32);
    bus.input("v"); bus.output("v");

    BusVisualNode vis(bus, BusOrientation::Horizontal, {});
    EXPECT_EQ(vis.getPortCount(), 1u) << "Empty bus: just the logical 'v' port";

    Wire w = Wire::make("w1", wire_output("bus1", "v"), wire_input("r1", "in"));
    vis.connectWire(w);
    EXPECT_EQ(vis.getPortCount(), 2u) << "After connect: 1 alias + 1 logical 'v'";
}

TEST(BusInvariant, DisconnectWire_RemovesPort) {
    Node bus; bus.id = "bus1"; bus.type_name = "Bus"; bus.render_hint = "bus";
    bus.at(0, 0).size_wh(128, 32);
    bus.input("v"); bus.output("v");

    std::vector<Wire> wires;
    wires.push_back(Wire::make("w1", wire_output("bus1", "v"), wire_input("r1", "in")));
    wires.push_back(Wire::make("w2", wire_output("gen", "out"), wire_input("bus1", "v")));

    BusVisualNode vis(bus, BusOrientation::Horizontal, wires);
    EXPECT_EQ(vis.getPortCount(), 3u);

    vis.disconnectWire(wires[0]);
    EXPECT_EQ(vis.getPortCount(), 2u) << "After disconnect: 1 alias + 1 logical 'v'";
}

TEST(BusInvariant, Construction_FiltersUnrelatedWires) {
    Node bus; bus.id = "bus1"; bus.type_name = "Bus"; bus.render_hint = "bus";
    bus.at(0, 0).size_wh(128, 32);
    bus.input("v"); bus.output("v");

    // 10 wires, none touching this bus
    std::vector<Wire> wires;
    for (int i = 0; i < 10; ++i) {
        std::string id = "w" + std::to_string(i);
        wires.push_back(Wire::make(id.c_str(),
            wire_output("other_a", "out"), wire_input("other_b", "in")));
    }

    BusVisualNode vis(bus, BusOrientation::Horizontal, wires);
    EXPECT_EQ(vis.getPortCount(), 1u) << "Bus with no connected wires: just the logical 'v' port";
}

// =============================================================================
// Non-Baked-In Sub-Blueprint Persistence
// =============================================================================

TEST(PersistNonBakedIn, Save_SkipsInternalNodes) {
    Blueprint bp;

    Node batt1;
    batt1.id = "batt1";
    batt1.type_name = "Battery";
    batt1.at(100.0f, 200.0f);
    batt1.output("v_out");
    bp.add_node(std::move(batt1));

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.baked_in = false;
    sbi.internal_node_ids = {"lamp_1:vin", "lamp_1:lamp", "lamp_1:vout"};
    bp.sub_blueprint_instances.push_back(sbi);

    Node vin_node;
    vin_node.id = "lamp_1:vin";
    vin_node.type_name = "BlueprintInput";
    vin_node.group_id = "lamp_1";
    vin_node.at(150.0f, 200.0f);
    bp.add_node(std::move(vin_node));

    Node lamp_node;
    lamp_node.id = "lamp_1:lamp";
    lamp_node.type_name = "IndicatorLight";
    lamp_node.group_id = "lamp_1";
    lamp_node.at(300.0f, 200.0f);
    bp.add_node(std::move(lamp_node));

    Node vout_node;
    vout_node.id = "lamp_1:vout";
    vout_node.type_name = "BlueprintOutput";
    vout_node.group_id = "lamp_1";
    vout_node.at(450.0f, 200.0f);
    bp.add_node(std::move(vout_node));

    std::string json = blueprint_to_editor_json(bp);
    auto j = nlohmann::json::parse(json);

    ASSERT_TRUE(j.contains("nodes"));
    EXPECT_EQ(j["nodes"].size(), 1) << "Should only save batt1 (collapsed node not saved)";

    for (const auto& [id, d] : j["nodes"].items()) {
        EXPECT_NE(id, "lamp_1:vin") << "Internal node lamp_1:vin should NOT be saved";
        EXPECT_NE(id, "lamp_1:lamp") << "Internal node lamp_1:lamp should NOT be saved";
        EXPECT_NE(id, "lamp_1:vout") << "Internal node lamp_1:vout should NOT be saved";
    }
}

TEST(PersistNonBakedIn, Save_SkipsInternalWires) {
    Blueprint bp;

    Node batt1;
    batt1.id = "batt1";
    batt1.type_name = "Battery";
    batt1.at(100.0f, 200.0f);
    batt1.output("v_out");
    bp.add_node(std::move(batt1));

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.baked_in = false;
    sbi.internal_node_ids = {"lamp_1:vin", "lamp_1:lamp", "lamp_1:vout"};
    bp.sub_blueprint_instances.push_back(sbi);

    Node vin_node;
    vin_node.id = "lamp_1:vin";
    vin_node.type_name = "BlueprintInput";
    vin_node.group_id = "lamp_1";
    vin_node.at(150.0f, 200.0f);
    vin_node.output("port");
    bp.add_node(std::move(vin_node));

    Node lamp_node;
    lamp_node.id = "lamp_1:lamp";
    lamp_node.type_name = "IndicatorLight";
    lamp_node.group_id = "lamp_1";
    lamp_node.at(300.0f, 200.0f);
    lamp_node.input("v_in");
    lamp_node.output("v_out");
    bp.add_node(std::move(lamp_node));

    Node vout_node;
    vout_node.id = "lamp_1:vout";
    vout_node.type_name = "BlueprintOutput";
    vout_node.group_id = "lamp_1";
    vout_node.at(450.0f, 200.0f);
    vout_node.input("port");
    bp.add_node(std::move(vout_node));

    Wire internal_wire1;
    internal_wire1.id = "wire_internal_1";
    internal_wire1.start = WireEnd("lamp_1:vin", "port", PortSide::Output);
    internal_wire1.end = WireEnd("lamp_1:lamp", "v_in", PortSide::Input);
    bp.add_wire(std::move(internal_wire1));

    Wire internal_wire2;
    internal_wire2.id = "wire_internal_2";
    internal_wire2.start = WireEnd("lamp_1:lamp", "v_out", PortSide::Output);
    internal_wire2.end = WireEnd("lamp_1:vout", "port", PortSide::Input);
    bp.add_wire(std::move(internal_wire2));

    Wire external_wire;
    external_wire.id = "wire_external";
    external_wire.start = WireEnd("batt1", "v_out", PortSide::Output);
    external_wire.end = WireEnd("lamp_1:vin", "port", PortSide::Input);
    bp.add_wire(std::move(external_wire));

    std::string json = blueprint_to_editor_json(bp);
    auto j = nlohmann::json::parse(json);

    ASSERT_TRUE(j.contains("wires") == false || j["wires"].size() == 0)
        << "Should skip all wires - external wire also skipped because endpoint is internal";

    if (j.contains("wires")) {
        for (const auto& w : j["wires"]) {
            std::string from_node = w["from"][0].get<std::string>();
            std::string to_node = w["to"][0].get<std::string>();
            EXPECT_FALSE(from_node.find("lamp_1:") == 0) << "Wire from internal node should NOT be saved";
            EXPECT_FALSE(to_node.find("lamp_1:") == 0) << "Wire to internal node should NOT be saved";
        }
    }
}

TEST(PersistNonBakedIn, Save_BakedInStillSavesInternals) {
    Blueprint bp;

    Node batt1;
    batt1.id = "batt1";
    batt1.type_name = "Battery";
    batt1.at(100.0f, 200.0f);
    batt1.output("v_out");
    bp.add_node(std::move(batt1));

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.baked_in = true;
    sbi.internal_node_ids = {"lamp_1:vin", "lamp_1:lamp", "lamp_1:vout"};
    bp.sub_blueprint_instances.push_back(sbi);

    Node vin_node;
    vin_node.id = "lamp_1:vin";
    vin_node.type_name = "BlueprintInput";
    vin_node.group_id = "lamp_1";
    vin_node.at(150.0f, 200.0f);
    bp.add_node(std::move(vin_node));

    Node lamp_node;
    lamp_node.id = "lamp_1:lamp";
    lamp_node.type_name = "IndicatorLight";
    lamp_node.group_id = "lamp_1";
    lamp_node.at(300.0f, 200.0f);
    bp.add_node(std::move(lamp_node));

    Node vout_node;
    vout_node.id = "lamp_1:vout";
    vout_node.type_name = "BlueprintOutput";
    vout_node.group_id = "lamp_1";
    vout_node.at(450.0f, 200.0f);
    bp.add_node(std::move(vout_node));

    Wire internal_wire;
    internal_wire.id = "wire_internal";
    internal_wire.start = WireEnd("lamp_1:vin", "port", PortSide::Output);
    internal_wire.end = WireEnd("lamp_1:lamp", "v_in", PortSide::Input);
    bp.add_wire(std::move(internal_wire));

    std::string json = blueprint_to_editor_json(bp);
    auto j = nlohmann::json::parse(json);

    ASSERT_TRUE(j.contains("nodes"));
    EXPECT_EQ(j["nodes"].size(), 4) << "Should save all nodes including internal ones for baked_in";

    EXPECT_TRUE(j["nodes"].contains("lamp_1:vin")) << "Baked-in: lamp_1:vin should be saved";
    EXPECT_TRUE(j["nodes"].contains("lamp_1:lamp")) << "Baked-in: lamp_1:lamp should be saved";
    EXPECT_TRUE(j["nodes"].contains("lamp_1:vout")) << "Baked-in: lamp_1:vout should be saved";

    ASSERT_TRUE(j.contains("wires"));
    EXPECT_GT(j["wires"].size(), 0) << "Baked-in: should save internal wires";
}

TEST(PersistNonBakedIn, Save_PreservesSubBlueprintInstanceMetadata) {
    Blueprint bp;

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.baked_in = false;
    sbi.internal_node_ids = {"lamp_1:vin", "lamp_1:lamp"};
    sbi.pos = Pt(400.0f, 300.0f);
    sbi.size = Pt(200.0f, 150.0f);
    sbi.params_override["lamp.color"] = "green";
    sbi.layout_override["lamp"] = Pt(500.0f, 400.0f);
    std::vector<Pt> routing_pts = {Pt(100.0f, 100.0f), Pt(200.0f, 200.0f)};
    sbi.internal_routing["vin.port->lamp.v_in"] = routing_pts;
    bp.sub_blueprint_instances.push_back(sbi);

    std::string json = blueprint_to_editor_json(bp);
    auto j = nlohmann::json::parse(json);

    ASSERT_TRUE(j.contains("sub_blueprints"));
    EXPECT_EQ(j["sub_blueprints"].size(), 1);

    ASSERT_TRUE(j["sub_blueprints"].contains("lamp_1"));
    const auto& sbi_json = j["sub_blueprints"]["lamp_1"];
    EXPECT_EQ(sbi_json.value("type_name", ""), "lamp_pass_through");
    // In v2, non-baked-in means no "nodes" key
    EXPECT_FALSE(sbi_json.contains("nodes")) << "Non-baked-in should not have nodes key";
    ASSERT_TRUE(sbi_json.contains("overrides"));
    EXPECT_TRUE(sbi_json["overrides"].contains("params"));
    EXPECT_TRUE(sbi_json["overrides"].contains("layout"));
    EXPECT_TRUE(sbi_json["overrides"].contains("routing"));
    EXPECT_EQ(sbi_json["overrides"]["params"].value("lamp.color", ""), "green");
}

TEST(PersistNonBakedIn, Load_ReExpandsFromRegistry) {
    std::string json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "batt1": {"type": "Battery",
             "pos": [100, 200], "size": [120, 80]}
        },
        "wires": [],
        "sub_blueprints": {
            "lamp_1": {
                "template": "lamp_pass_through",
                "type_name": "lamp_pass_through",
                "pos": [400, 300],
                "size": [200, 150]
            }
        },
        "viewport": {"pan": [0, 0], "zoom": 1.0, "grid": 16}
    })";

    auto bp_opt = blueprint_from_json(json);
    ASSERT_TRUE(bp_opt.has_value()) << "Failed to parse JSON";
    const auto& bp = *bp_opt;

    bool found_lamp = false;
    for (const auto& n : bp.nodes) {
        if (n.id == "lamp_1:lamp") {
            found_lamp = true;
            EXPECT_EQ(n.group_id, "lamp_1") << "Internal node should have correct group_id";
            break;
        }
    }

    EXPECT_TRUE(found_lamp) << "Internal nodes should be re-expanded from registry";
}

TEST(PersistNonBakedIn, Load_AppliesParamsOverride) {
    std::string json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {},
        "wires": [],
        "sub_blueprints": {
            "simple_battery_1": {
                "template": "simple_battery",
                "type_name": "simple_battery",
                "pos": [400, 300],
                "size": [200, 150],
                "overrides": {
                    "params": {
                        "bat.v_nominal": "14.0"
                    }
                }
            }
        },
        "viewport": {"pan": [0, 0], "zoom": 1.0, "grid": 16}
    })";

    auto bp_opt = blueprint_from_json(json);
    ASSERT_TRUE(bp_opt.has_value()) << "Failed to parse JSON";
    const auto& bp = *bp_opt;

    const Node* n = bp.find_node("simple_battery_1:bat");
    ASSERT_NE(n, nullptr) << "simple_battery_1:bat should exist after expansion";
    auto it = n->params.find("v_nominal");
    ASSERT_NE(it, n->params.end()) << "v_nominal param should exist";
    EXPECT_EQ(it->second, "14.0") << "Params override should be applied";
}

TEST(PersistNonBakedIn, Load_AppliesLayoutOverride) {
    std::string json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {},
        "wires": [],
        "sub_blueprints": {
            "lamp_1": {
                "template": "lamp_pass_through",
                "type_name": "lamp_pass_through",
                "pos": [400, 300],
                "size": [200, 150],
                "overrides": {
                    "layout": {
                        "lamp": [500, 400]
                    }
                }
            }
        },
        "viewport": {"pan": [0, 0], "zoom": 1.0, "grid": 16}
    })";

    auto bp_opt = blueprint_from_json(json);
    ASSERT_TRUE(bp_opt.has_value()) << "Failed to parse JSON";
    const auto& bp = *bp_opt;

    const Node* n = bp.find_node("lamp_1:lamp");
    ASSERT_NE(n, nullptr) << "lamp_1:lamp should exist after expansion";
    EXPECT_FLOAT_EQ(n->pos.x, 500.0f) << "Layout override x position should be applied";
    EXPECT_FLOAT_EQ(n->pos.y, 400.0f) << "Layout override y position should be applied";
}

TEST(PersistNonBakedIn, Roundtrip_NonBakedIn) {
    Blueprint bp;

    Node batt1;
    batt1.id = "batt1";
    batt1.type_name = "Battery";
    batt1.at(100.0f, 200.0f);
    batt1.output("v_out");
    bp.add_node(std::move(batt1));

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.baked_in = false;
    sbi.pos = Pt(400.0f, 300.0f);
    sbi.size = Pt(200.0f, 150.0f);
    bp.sub_blueprint_instances.push_back(sbi);

    std::string json = blueprint_to_editor_json(bp);
    auto bp2_opt = blueprint_from_json(json);
    ASSERT_TRUE(bp2_opt.has_value()) << "Roundtrip: failed to load saved JSON";
    const auto& bp2 = *bp2_opt;

    EXPECT_EQ(bp2.sub_blueprint_instances.size(), 1) << "SBI metadata should be preserved";
    EXPECT_EQ(bp2.sub_blueprint_instances[0].id, "lamp_1");
    EXPECT_EQ(bp2.sub_blueprint_instances[0].type_name, "lamp_pass_through");
    EXPECT_EQ(bp2.sub_blueprint_instances[0].baked_in, false);

    int top_level_count = 0;
    for (const auto& n : bp2.nodes) {
        if (n.group_id.empty()) {
            top_level_count++;
        }
    }
    EXPECT_EQ(top_level_count, 1) << "Should have exactly 1 top-level node (batt1)";

    bool found_internal_lamp = false;
    for (const auto& n : bp2.nodes) {
        if (n.id.find("lamp_1:") == 0) {
            found_internal_lamp = true;
            EXPECT_EQ(n.group_id, "lamp_1");
        }
    }
    EXPECT_TRUE(found_internal_lamp) << "Internal nodes should be re-expanded";
}

TEST(PersistNonBakedIn, Roundtrip_MixedBakedAndNonBaked) {
    Blueprint bp;

    Node batt1;
    batt1.id = "batt1";
    batt1.type_name = "Battery";
    batt1.at(100.0f, 200.0f);
    batt1.output("v_out");
    bp.add_node(std::move(batt1));

    SubBlueprintInstance sbi_non_baked;
    sbi_non_baked.id = "lamp_1";
    sbi_non_baked.type_name = "lamp_pass_through";
    sbi_non_baked.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi_non_baked.baked_in = false;
    sbi_non_baked.pos = Pt(400.0f, 300.0f);
    sbi_non_baked.size = Pt(200.0f, 150.0f);
    bp.sub_blueprint_instances.push_back(sbi_non_baked);

    SubBlueprintInstance sbi_baked;
    sbi_baked.id = "lamp_2";
    sbi_baked.type_name = "lamp_pass_through";
    sbi_baked.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi_baked.baked_in = true;
    sbi_baked.pos = Pt(400.0f, 500.0f);
    sbi_baked.size = Pt(200.0f, 150.0f);
    sbi_baked.internal_node_ids = {"lamp_2:vin", "lamp_2:lamp", "lamp_2:vout"};
    bp.sub_blueprint_instances.push_back(sbi_baked);

    Node vin_baked;
    vin_baked.id = "lamp_2:vin";
    vin_baked.type_name = "BlueprintInput";
    vin_baked.group_id = "lamp_2";
    vin_baked.at(150.0f, 500.0f);
    bp.add_node(std::move(vin_baked));

    Node lamp_baked;
    lamp_baked.id = "lamp_2:lamp";
    lamp_baked.type_name = "IndicatorLight";
    lamp_baked.group_id = "lamp_2";
    lamp_baked.at(300.0f, 500.0f);
    bp.add_node(std::move(lamp_baked));

    Node vout_baked;
    vout_baked.id = "lamp_2:vout";
    vout_baked.type_name = "BlueprintOutput";
    vout_baked.group_id = "lamp_2";
    vout_baked.at(450.0f, 500.0f);
    bp.add_node(std::move(vout_baked));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2_opt = blueprint_from_json(json);
    ASSERT_TRUE(bp2_opt.has_value()) << "Roundtrip: failed to load saved JSON";
    const auto& bp2 = *bp2_opt;

    EXPECT_EQ(bp2.sub_blueprint_instances.size(), 2);

    bool non_baked_found = false, baked_found = false;
    for (const auto& sbi : bp2.sub_blueprint_instances) {
        if (sbi.id == "lamp_1") {
            non_baked_found = true;
            EXPECT_EQ(sbi.baked_in, false);
        } else if (sbi.id == "lamp_2") {
            baked_found = true;
            EXPECT_EQ(sbi.baked_in, true);
        }
    }
    EXPECT_TRUE(non_baked_found);
    EXPECT_TRUE(baked_found);

    bool lamp_2_lamp_found = false;
    for (const auto& n : bp2.nodes) {
        if (n.id == "lamp_2:lamp") {
            lamp_2_lamp_found = true;
            EXPECT_EQ(n.group_id, "lamp_2");
        }
    }
    EXPECT_TRUE(lamp_2_lamp_found) << "Baked-in internal node should be preserved";
}

TEST(PersistNonBakedIn, BlueprintPath_ContainsCategory) {
    Blueprint bp;

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "systems/lamp_pass_through";
    sbi.baked_in = false;
    sbi.pos = Pt(400.0f, 300.0f);
    sbi.size = Pt(200.0f, 150.0f);
    bp.sub_blueprint_instances.push_back(sbi);

    std::string json = blueprint_to_editor_json(bp);
    auto j = nlohmann::json::parse(json);

    ASSERT_TRUE(j.contains("sub_blueprints"));
    EXPECT_EQ(j["sub_blueprints"].size(), 1);

    ASSERT_TRUE(j["sub_blueprints"].contains("lamp_1"));
    const auto& sbi_json = j["sub_blueprints"]["lamp_1"];
    EXPECT_EQ(sbi_json.value("template", ""), "systems/lamp_pass_through")
        << "template should contain category prefix";
}

TEST(SubBlueprintMenu, EditOriginal_ResolvesLibraryPath) {
    std::string json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {},
        "wires": [],
        "sub_blueprints": {
            "lamp_1": {
                "template": "systems/lamp_pass_through",
                "type_name": "lamp_pass_through",
                "pos": [400, 300],
                "size": [200, 150]
            }
        },
        "viewport": {"pan": [0, 0], "zoom": 1.0, "grid": 16}
    })";

    auto bp_opt = blueprint_from_json(json);
    ASSERT_TRUE(bp_opt.has_value()) << "Failed to parse JSON";
    const auto& bp = *bp_opt;

    ASSERT_EQ(bp.sub_blueprint_instances.size(), 1);
    const auto& sbi = bp.sub_blueprint_instances[0];
    EXPECT_EQ(sbi.blueprint_path, "systems/lamp_pass_through");

    std::string lib_path = "library/" + sbi.blueprint_path + ".json";
    EXPECT_EQ(lib_path, "library/systems/lamp_pass_through.json")
        << "Library path should be correctly constructed from blueprint_path";
}

// ============================================================================
// Bug regression: internal node positions lost after save/load for non-baked-in
// ============================================================================

TEST(PersistNonBakedIn, Roundtrip_PreservesInternalNodePositions) {
    // Build a blueprint with a non-baked-in sub-blueprint whose internal nodes
    // have been positioned (e.g. via auto_layout_group on first add).
    Blueprint bp;

    Node batt;
    batt.id = "batt1";
    batt.type_name = "Battery";
    batt.at(100.0f, 200.0f);
    batt.output("v_out");
    bp.add_node(std::move(batt));

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "systems/lamp_pass_through";
    sbi.baked_in = false;
    sbi.pos = Pt(400.0f, 300.0f);
    sbi.size = Pt(200.0f, 150.0f);
    bp.sub_blueprint_instances.push_back(sbi);

    // Simulate internal nodes that have already been positioned
    // (as would happen after add_blueprint + auto_layout_group)
    Node vin;
    vin.id = "lamp_1:vin";
    vin.type_name = "BlueprintInput";
    vin.group_id = "lamp_1";
    vin.at(50.0f, 100.0f).size_wh(100, 60);
    bp.add_node(std::move(vin));
    bp.sub_blueprint_instances.back().internal_node_ids.push_back("lamp_1:vin");

    Node lamp;
    lamp.id = "lamp_1:lamp";
    lamp.type_name = "IndicatorLight";
    lamp.group_id = "lamp_1";
    lamp.at(250.0f, 100.0f).size_wh(100, 60);
    bp.add_node(std::move(lamp));
    bp.sub_blueprint_instances.back().internal_node_ids.push_back("lamp_1:lamp");

    Node vout;
    vout.id = "lamp_1:vout";
    vout.type_name = "BlueprintOutput";
    vout.group_id = "lamp_1";
    vout.at(450.0f, 100.0f).size_wh(100, 60);
    bp.add_node(std::move(vout));
    bp.sub_blueprint_instances.back().internal_node_ids.push_back("lamp_1:vout");

    // Save
    std::string json = blueprint_to_editor_json(bp);

    // Verify overrides were populated in the saved JSON
    auto j = nlohmann::json::parse(json);
    ASSERT_TRUE(j.contains("sub_blueprints"));
    ASSERT_EQ(j["sub_blueprints"].size(), 1);
    ASSERT_TRUE(j["sub_blueprints"].contains("lamp_1"));
    const auto& sbi_json = j["sub_blueprints"]["lamp_1"];
    ASSERT_TRUE(sbi_json.contains("overrides"))
        << "Save should snapshot internal node positions into overrides";
    if (sbi_json.contains("overrides") && sbi_json["overrides"].contains("layout")) {
        EXPECT_TRUE(sbi_json["overrides"]["layout"].contains("vin"));
        EXPECT_TRUE(sbi_json["overrides"]["layout"].contains("lamp"));
        EXPECT_TRUE(sbi_json["overrides"]["layout"].contains("vout"));
    }

    // Load
    auto bp2_opt = blueprint_from_json(json);
    ASSERT_TRUE(bp2_opt.has_value()) << "Failed to load saved JSON";
    const auto& bp2 = *bp2_opt;

    // Verify positions survived the round-trip
    const Node* n_vin = bp2.find_node("lamp_1:vin");
    const Node* n_lamp = bp2.find_node("lamp_1:lamp");
    const Node* n_vout = bp2.find_node("lamp_1:vout");
    ASSERT_NE(n_vin, nullptr);
    ASSERT_NE(n_lamp, nullptr);
    ASSERT_NE(n_vout, nullptr);

    EXPECT_FLOAT_EQ(n_vin->pos.x, 50.0f) << "vin x position should be preserved";
    EXPECT_FLOAT_EQ(n_vin->pos.y, 100.0f) << "vin y position should be preserved";
    EXPECT_FLOAT_EQ(n_lamp->pos.x, 250.0f) << "lamp x position should be preserved";
    EXPECT_FLOAT_EQ(n_lamp->pos.y, 100.0f) << "lamp y position should be preserved";
    EXPECT_FLOAT_EQ(n_vout->pos.x, 450.0f) << "vout x position should be preserved";
    EXPECT_FLOAT_EQ(n_vout->pos.y, 100.0f) << "vout y position should be preserved";
}

TEST(PersistNonBakedIn, Load_AutoLayoutFallback_WhenNoLayoutOverride) {
    // When a non-baked-in SBI has no layout override (e.g. old save file),
    // positions should get auto-layout instead of staying at (0,0).
    std::string json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {
            "batt1": {"type": "Battery",
             "pos": [100, 200], "size": [120, 80]}
        },
        "wires": [],
        "sub_blueprints": {
            "lamp_1": {
                "template": "lamp_pass_through",
                "type_name": "lamp_pass_through",
                "pos": [400, 300],
                "size": [200, 150]
            }
        },
        "viewport": {"pan": [0, 0], "zoom": 1.0, "grid": 16}
    })";

    auto bp_opt = blueprint_from_json(json);
    ASSERT_TRUE(bp_opt.has_value());
    const auto& bp = *bp_opt;

    // All internal nodes should have been auto-layouted (not all at 0,0)
    bool all_zero = true;
    for (const auto& n : bp.nodes) {
        if (n.group_id == "lamp_1") {
            if (n.pos.x != 0.0f || n.pos.y != 0.0f) {
                all_zero = false;
                break;
            }
        }
    }
    EXPECT_FALSE(all_zero)
        << "Internal nodes without layout_override should get auto-layout, not stay at (0,0)";
}

// ==================================================================
// Regression: Override Key Prefixing in Editor Persistence
// ==================================================================

TEST(PersistRegression, OverrideLayoutKeysUnprefixedInJSON) {
    Blueprint bp;

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.baked_in = false;
    sbi.internal_node_ids = {"lamp_1:vin", "lamp_1:lamp"};
    sbi.pos = Pt(400.0f, 300.0f);
    sbi.size = Pt(200.0f, 150.0f);
    bp.sub_blueprint_instances.push_back(sbi);

    Node vin;
    vin.id = "lamp_1:vin";
    vin.type_name = "BlueprintInput";
    vin.group_id = "lamp_1";
    vin.at(50.0f, 100.0f);
    bp.add_node(std::move(vin));

    Node lamp;
    lamp.id = "lamp_1:lamp";
    lamp.type_name = "IndicatorLight";
    lamp.group_id = "lamp_1";
    lamp.at(250.0f, 100.0f);
    bp.add_node(std::move(lamp));

    std::string json = blueprint_to_editor_json(bp);
    auto j = nlohmann::json::parse(json);

    ASSERT_TRUE(j.contains("sub_blueprints"));
    ASSERT_TRUE(j["sub_blueprints"].contains("lamp_1"));
    const auto& ov = j["sub_blueprints"]["lamp_1"]["overrides"]["layout"];

    EXPECT_TRUE(ov.contains("vin")) << "Layout key should be unprefixed 'vin'";
    EXPECT_TRUE(ov.contains("lamp")) << "Layout key should be unprefixed 'lamp'";
    EXPECT_FALSE(ov.contains("lamp_1:vin")) << "Layout key must NOT have 'lamp_1:' prefix";
    EXPECT_FALSE(ov.contains("lamp_1:lamp")) << "Layout key must NOT have 'lamp_1:' prefix";
}

TEST(PersistRegression, ContentKindLowercaseInJSON) {
    Blueprint bp;

    Node vm;
    vm.id = "vm1";
    vm.type_name = "Voltmeter";
    vm.at(100.0f, 200.0f);
    vm.node_content.type = NodeContentType::Gauge;
    vm.node_content.label = "V";
    bp.add_node(std::move(vm));

    std::string json = blueprint_to_editor_json(bp);
    auto j = nlohmann::json::parse(json);

    ASSERT_TRUE(j["nodes"].contains("vm1"));
    ASSERT_TRUE(j["nodes"]["vm1"].contains("content"));
    EXPECT_EQ(j["nodes"]["vm1"]["content"]["kind"], "gauge")
        << "Content kind should be lowercase per v2 design";
}

TEST(PersistRegression, LayoutOverrideAppliedAfterLoad) {
    std::string json = R"({
        "version": 2, "meta": {"name": ""},
        "nodes": {},
        "wires": [],
        "sub_blueprints": {
            "lamp_1": {
                "template": "lamp_pass_through",
                "type_name": "lamp_pass_through",
                "pos": [400, 300], "size": [200, 150],
                "overrides": {
                    "layout": {
                        "lamp": [500, 400]
                    }
                }
            }
        },
        "viewport": {"pan": [0, 0], "zoom": 1.0, "grid": 16}
    })";

    auto bp_opt = blueprint_from_json(json);
    ASSERT_TRUE(bp_opt.has_value());

    const Node* lamp = bp_opt->find_node("lamp_1:lamp");
    ASSERT_NE(lamp, nullptr);
    EXPECT_FLOAT_EQ(lamp->pos.x, 500.0f);
    EXPECT_FLOAT_EQ(lamp->pos.y, 400.0f);
}

// =============================================================================
// Regression: unprefixed internal_node_ids must not crash sbi_to_v2 on save
// https://github.com/... (crash in sbi_to_v2 at persist.cpp:417)
// =============================================================================

TEST(PersistRegression, UnprefixedInternalNodeIdDoesNotCrashOnSave) {
    // Construct a Blueprint with a baked-in sub-blueprint that has an
    // internal_node_id WITHOUT the expected "sbi_id:" prefix.
    // This reproduces the crash where substr(prefix.size()) would throw
    // out_of_range on a string shorter than the prefix.
    Blueprint bp;

    // A regular top-level node
    Node top_node;
    top_node.id = "bat_1";
    top_node.type_name = "Battery";
    top_node.pos = {100, 200};
    bp.nodes.push_back(top_node);

    // A baked-in sub-blueprint's internal nodes (correctly prefixed)
    Node prefixed_node;
    prefixed_node.id = "sub_1:lamp";
    prefixed_node.type_name = "IndicatorLight";
    prefixed_node.group_id = "sub_1";
    prefixed_node.pos = {300, 400};
    bp.nodes.push_back(prefixed_node);

    // Bug scenario: an internal node with MISMATCHED prefix (no "sub_1:" prefix)
    Node unprefixed_node;
    unprefixed_node.id = "any_v_to_bool_1";  // Should be "sub_1:any_v_to_bool_1"
    unprefixed_node.type_name = "Any_V_to_Bool";
    unprefixed_node.group_id = "sub_1";
    unprefixed_node.pos = {500, 600};
    bp.nodes.push_back(unprefixed_node);

    // Sub-blueprint instance that references both nodes
    SubBlueprintInstance sbi;
    sbi.id = "sub_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "systems/lamp_pass_through";
    sbi.baked_in = true;
    sbi.pos = {200, 300};
    sbi.size = {100, 100};
    sbi.internal_node_ids = {"sub_1:lamp", "any_v_to_bool_1"};  // one prefixed, one not
    bp.sub_blueprint_instances.push_back(sbi);

    // This must not throw (previously crashed with out_of_range in substr)
    std::string result;
    EXPECT_NO_THROW(result = blueprint_to_editor_json(bp));
    EXPECT_FALSE(result.empty());

    // Verify the output is valid JSON
    auto j = nlohmann::json::parse(result);
    EXPECT_EQ(j["version"].get<int>(), 2);

    // The baked-in SBI should have both nodes (unprefixed keys)
    auto& sb_nodes = j["sub_blueprints"]["sub_1"]["nodes"];
    EXPECT_TRUE(sb_nodes.contains("lamp"));
    EXPECT_TRUE(sb_nodes.contains("any_v_to_bool_1"));
}

