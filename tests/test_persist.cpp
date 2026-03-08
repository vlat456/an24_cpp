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
        "devices": [
            {"name": "batt", "classname": "Battery", "kind": "Node",
             "ports": {"v_in": {"direction": "In", "type": "V"}, "v_out": {"direction": "Out", "type": "V"}},
             "pos": {"x": 50, "y": 60}, "size": {"x": 120, "y": 80}},
            {"name": "load", "classname": "Resistor", "kind": "Node",
             "ports": {"v_in": {"direction": "In", "type": "V"}},
             "pos": {"x": 250, "y": 60}, "size": {"x": 100, "y": 60}}
        ],
        "wires": [
            {"from": "batt.v_out", "to": "load.v_in",
             "routing_points": [{"x": 150, "y": 60}, {"x": 150, "y": 100}]}
        ],
        "viewport": {"pan": {"x": 100, "y": 200}, "zoom": 2.0, "grid_step": 32}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());

    // Check devices converted to nodes
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

    std::string json = blueprint_to_editor_json(bp);
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

    std::string json = blueprint_to_editor_json(bp);
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

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);
    EXPECT_EQ(bp2->nodes[0].kind, NodeKind::Node);
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
        {"ports", {{"v_out", {{"direction", "Out"}, {"type", "V"}}}}},
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

// =============================================================================
// BUGFIX [e4a1b7] Dedup guard regression tests
// =============================================================================

TEST(PersistTest, DedupGuard_DuplicateWiresDroppedOnLoad) {
    const char* json = R"({
        "devices": [
            {"name": "a", "classname": "Battery", "kind": "Node",
             "ports": {"v_out": {"direction": "Out", "type": "V"}},
             "pos": {"x": 0, "y": 0}, "size": {"x": 120, "y": 80}},
            {"name": "b", "classname": "Resistor", "kind": "Node",
             "ports": {"v_in": {"direction": "In", "type": "V"}},
             "pos": {"x": 200, "y": 0}, "size": {"x": 120, "y": 80}}
        ],
        "wires": [
            {"from": "a.v_out", "to": "b.v_in", "routing_points": []},
            {"from": "a.v_out", "to": "b.v_in", "routing_points": []},
            {"from": "a.v_out", "to": "b.v_in", "routing_points": []},
            {"from": "a.v_out", "to": "b.v_in", "routing_points": []}
        ],
        "viewport": {"pan": {"x": 0, "y": 0}, "zoom": 1.0, "grid_step": 16}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    EXPECT_EQ(bp->wires.size(), 1)
        << "Duplicate wires must be deduped on load (had 4 identical wires)";
}

TEST(PersistTest, DedupGuard_DuplicateNodesDroppedOnLoad) {
    const char* json = R"({
        "devices": [
            {"name": "x", "classname": "Battery", "kind": "Node",
             "ports": {"v_out": {"direction": "Out", "type": "V"}},
             "pos": {"x": 0, "y": 0}, "size": {"x": 120, "y": 80}},
            {"name": "x", "classname": "Battery", "kind": "Node",
             "ports": {"v_out": {"direction": "Out", "type": "V"}},
             "pos": {"x": 100, "y": 0}, "size": {"x": 120, "y": 80}}
        ],
        "wires": [],
        "viewport": {"pan": {"x": 0, "y": 0}, "zoom": 1.0, "grid_step": 16}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    EXPECT_EQ(bp->nodes.size(), 1)
        << "Duplicate node IDs must be deduped on load";
}

TEST(PersistTest, DedupGuard_DuplicateRoutingPointsDroppedOnLoad) {
    const char* json = R"({
        "devices": [
            {"name": "a", "classname": "Battery", "kind": "Node",
             "ports": {"v_out": {"direction": "Out", "type": "V"}},
             "pos": {"x": 0, "y": 0}, "size": {"x": 120, "y": 80}},
            {"name": "b", "classname": "Resistor", "kind": "Node",
             "ports": {"v_in": {"direction": "In", "type": "V"}},
             "pos": {"x": 200, "y": 0}, "size": {"x": 120, "y": 80}}
        ],
        "wires": [
            {"from": "a.v_out", "to": "b.v_in",
             "routing_points": [{"x": 100, "y": 50}, {"x": 100, "y": 50}, {"x": 150, "y": 50}]}
        ],
        "viewport": {"pan": {"x": 0, "y": 0}, "zoom": 1.0, "grid_step": 16}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->wires.size(), 1);
    EXPECT_EQ(bp->wires[0].routing_points.size(), 2)
        << "Duplicate routing points must be deduped on load";
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
// BUGFIX [d9c3f2] save_blueprint_to_file rejects blueprints/ directory
// =============================================================================

TEST(PersistTest, SaveRejectsBluprintDirectory) {
    Blueprint bp;
    // Should refuse to write to blueprints/ directory
    bool ok = save_blueprint_to_file(bp, "blueprints/test_output.json");
    EXPECT_FALSE(ok) << "save_blueprint_to_file must refuse to write into blueprints/ dir";
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
// Editor save dedup: collapsed_groups dedup
// =============================================================================

TEST(PersistTest, EditorSave_DedupsCollapsedGroups) {
    Blueprint bp;

    CollapsedGroup g1("lamp1", "blueprints/lamp.json", "LampCircuit");
    CollapsedGroup g2("lamp1", "blueprints/lamp.json", "LampCircuit"); // exact dup

    bp.collapsed_groups.push_back(g1);
    bp.collapsed_groups.push_back(g2);

    std::string json_str = blueprint_to_editor_json(bp);
    auto j = nlohmann::json::parse(json_str);

    ASSERT_TRUE(j.contains("collapsed_groups"));
    EXPECT_EQ(j["collapsed_groups"].size(), 1)
        << "Duplicate collapsed_groups must be deduped on save";
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
    Node gnd; gnd.id = "g1:gnd"; gnd.type_name = "RefNode"; gnd.kind = NodeKind::Ref; gnd.group_id = "g1";

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
    Node bus;  bus.id = "g:bus";  bus.type_name = "Bus"; bus.kind = NodeKind::Bus; bus.group_id = "g";
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
    bool ok = save_blueprint_to_file(bp, "/some/path/blueprints/nested/test.json");
    EXPECT_FALSE(ok) << "Must reject any path with 'blueprints' segment";
}

TEST(PersistTest, SaveAllowsNonBlueprintDirectory) {
    Blueprint bp;
    Node a; a.id = "a"; a.type_name = "Battery"; a.output("v_out"); a.at(0, 0);
    bp.add_node(std::move(a));

    // Save to /tmp — should succeed (path doesn't contain "blueprints")
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
// BUGFIX [c3d4e5] Kind inference from classname — RefNode gets kind=Ref on load
// =============================================================================

TEST(PersistTest, KindInference_RefNodeGetsRefKind) {
    // JSON with RefNode classname but kind="Node" (corrupted save from old code)
    const char* json = R"({
        "devices": [
            {"name": "gnd", "classname": "RefNode", "kind": "Node",
             "ports": {"v": {"direction": "Out", "type": "V"}},
             "params": {"value": "0.0"},
             "pos": {"x": 0, "y": 0}, "size": {"x": 48, "y": 48}}
        ],
        "wires": [],
        "viewport": {"pan": {"x": 0, "y": 0}, "zoom": 1.0, "grid_step": 16}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1);
    EXPECT_EQ(bp->nodes[0].kind, NodeKind::Ref)
        << "RefNode classname must produce kind=Ref regardless of JSON kind field";
}

TEST(PersistTest, KindInference_BusGetsCorrectKind) {
    const char* json = R"({
        "devices": [
            {"name": "bus1", "classname": "Bus", "kind": "Node",
             "ports": {"v": {"direction": "InOut", "type": "V"}},
             "pos": {"x": 0, "y": 0}, "size": {"x": 48, "y": 48}}
        ],
        "wires": [],
        "viewport": {"pan": {"x": 0, "y": 0}, "zoom": 1.0, "grid_step": 16}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1);
    EXPECT_EQ(bp->nodes[0].kind, NodeKind::Bus)
        << "Bus classname must produce kind=Bus regardless of JSON kind field";
}

TEST(PersistTest, KindInference_BlueprintKindPreserved) {
    const char* json = R"({
        "devices": [
            {"name": "lamp1", "classname": "lamp_pass_through", "kind": "Blueprint",
             "ports": {"vin": {"direction": "In", "type": "V"}},
             "pos": {"x": 0, "y": 0}, "size": {"x": 128, "y": 96}}
        ],
        "wires": [],
        "viewport": {"pan": {"x": 0, "y": 0}, "zoom": 1.0, "grid_step": 16}
    })";

    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    ASSERT_EQ(bp->nodes.size(), 1);
    EXPECT_EQ(bp->nodes[0].kind, NodeKind::Blueprint)
        << "Explicit Blueprint kind must be preserved (can't be inferred from classname)";
}

TEST(PersistTest, KindInference_Roundtrip_RefNodePreservesKind) {
    Blueprint bp;
    Node gnd;
    gnd.id = "gnd";
    gnd.type_name = "RefNode";
    gnd.kind = NodeKind::Ref;
    gnd.output("v");
    gnd.at(0, 0);
    bp.add_node(std::move(gnd));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);
    EXPECT_EQ(bp2->nodes[0].kind, NodeKind::Ref)
        << "RefNode kind must survive save/load roundtrip";
}

TEST(PersistTest, KindInference_CorruptedRefNodeFixedOnRoundtrip) {
    // Simulate corrupted blueprint: RefNode with kind=Node
    Blueprint bp;
    Node gnd;
    gnd.id = "gnd";
    gnd.type_name = "RefNode";
    gnd.kind = NodeKind::Node; // intentionally wrong
    gnd.output("v");
    gnd.at(0, 0);
    bp.nodes.push_back(std::move(gnd)); // bypass add_node to keep wrong kind

    // Save preserves wrong kind (because it reads from node.kind)
    std::string json = blueprint_to_editor_json(bp);
    // But load should fix it based on classname
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1);
    EXPECT_EQ(bp2->nodes[0].kind, NodeKind::Ref)
        << "Load must fix corrupted RefNode kind based on classname";
}


